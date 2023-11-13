#include "../catch.hpp"

#define LOGURU_DEBUG_LOGGING 1
#include <loguru.hpp>
#include <ftl/profiler.hpp>

#include <map>

#include <msquic.h>
#include "msquic/msquichelper.hpp"

#include "openssl_util.hpp"
#include "quic.hpp"

using namespace beyond_impl;

#define HOST "127.0.0.1"

/** Quic client for a single stream */
class TestQuicClient : 
    public IMsQuicConnectionHandler,
    public IMsQuicStreamHandler
{
private:
    struct WriteEvent
    {
        std::vector<QUIC_BUFFER> Buffers;
        std::promise<bool> Promise;
        bool Complete = false;
    };

    std::mutex WriteMtx;
    std::deque<WriteEvent> WriteQueue;

public:
    TestQuicClient() {}

    std::mutex Mtx;
    std::condition_variable Cv;
    int ConnectEventCount = 0;
    int DisconnectEventCount = 0;

    std::atomic_int RecvdTotal = 0;
    
    // Connection Observer

    void OnConnect(MsQuicConnection* Connection) override
    {
        auto Lk = std::unique_lock(Mtx);
        DLOG(INFO) << "[" << this << "] " << "OnConnect";
        ConnectEventCount++;
        Cv.notify_all();
    }

    void OnDisconnect(MsQuicConnection* Connection) override
    {
        auto Lk = std::unique_lock(Mtx);
        DLOG(INFO) << "[" << this << "] " << "OnDisconnect";
        DisconnectEventCount++;
        Cv.notify_all();
    }

    void OnCertificateReceived(MsQuicConnection* Connection, QUIC_BUFFER* Certificate, QUIC_BUFFER* Chain)
    {
        if (Certificate)
        {
            DLOG(INFO) << "[" << this << "] " << "OnCertificateReceived()";
        }
        else 
        {
            DLOG(INFO) << "[" << this << "] " << "OnCertificateReceived(): empty";
        }
    }

    // Stream Observer

    void OnData(MsQuicStream* Stream, nonstd::span<const QUIC_BUFFER> Data) override
    {
        uint32_t Count = 0;
        for (const auto& Buffer : Data)
        {
            Count += Buffer.Length;
        }
        RecvdTotal += Count;
        Stream->Consume(Count);
    }

    void OnWriteComplete(MsQuicStream* stream, void* Context, bool Cancelled) override
    {
        DLOG(INFO) << "[" << this << "] " << "OnWriteComplete";
        std::unique_lock<std::mutex> Lock(WriteMtx);
        auto* Event = static_cast<WriteEvent*>(Context);
        Event->Complete = true;
        Event->Promise.set_value(!Cancelled);
        while (WriteQueue.size() > 0 && WriteQueue.back().Complete)
        {
            WriteQueue.pop_back();
        }
    }

    std::future<bool> Write(MsQuicStream* Stream, nonstd::span<beyond_impl::Bytes> Data)
    {
        DLOG(INFO) << "[" << this << "] " << "Write";
        std::unique_lock<std::mutex> Lock(WriteMtx);
        WriteEvent& Event = WriteQueue.emplace_front();
        for (auto Span : Data)
        {
            Event.Buffers.push_back({(uint32_t)Span.size(), Span.data()});
        }
        
        CHECK(Stream->Write({Event.Buffers.data(), Event.Buffers.size()}, &Event));
        return Event.Promise.get_future();
    }

    TestQuicClient(const TestQuicClient&) = delete;
    TestQuicClient& operator=(const TestQuicClient&) = delete;
};

class TestQuicServer : public MsQuicServer, public TestQuicClient
{
public:
    TestQuicServer(MsQuicContext* Context) : MsQuicServer(Context) {}

    // Server's handles for Connection and Stream
    MsQuicConnectionPtr Connection;
    MsQuicStreamPtr Stream;

    // signaled when stream is set up
    std::promise<void> ClientConnected;

    // server callbacks
    
    void OnConnection(MsQuicConnectionPtr ConnectionIn, const QUIC_NEW_CONNECTION_INFO& Info) override
    {
        DLOG(INFO) << "[" << this << "] " << "OnConnection";
        Connection = std::move(ConnectionIn);
        Connection->SetConnectionObserver(this);
    }

    // connection callbacks

    void OnStreamCreate(MsQuicConnection* Connection, MsQuicStreamPtr StreamIn) override
    {
        DLOG(INFO) << "[" << this << "] " << "OnStreamCreate";
        Stream = std::move(StreamIn);
        Stream->SetStreamHandler(this);
        Stream->EnableRecv();
        ClientConnected.set_value();
    }

    TestQuicServer(const TestQuicServer&) = delete;
    TestQuicServer& operator=(const TestQuicServer&) = delete;
};


static std::unique_ptr<beyond_impl::MsQuicContext> Context_;

beyond_impl::MsQuicContext* GetContext()
{
    if (!Context_)
    {
        auto Ptr = std::make_unique<beyond_impl::MsQuicContext>();
        beyond_impl::MsQuicContext::Open(*Ptr, "beyond2");
        Context_ = std::move(Ptr);

    }
    return Context_.get();
}

void ResetContext()
{
    if (Context_)
    {
        MsQuicContext::Close(*Context_);
        Context_.reset();
    }
}

static std::vector<unsigned char> Asn1Blob;

TEST_CASE("Reinitialize")
{
    auto* Ctx = GetContext();
    REQUIRE(Ctx != nullptr);
    ResetContext();
    // On Linux this fails (pointer is the same after closing and opening again) but MsQuicClose/MsQuicOpen are
    // succesful (and called the expected number of times). TODO: Needs a better check here (otherwise may deadlock
    // on unload etc when all resources are not properly released).
    // REQUIRE(Ctx != GetContext()); 
}

TEST_CASE("Self signed certificate")
{
    CertificateParams params;
    CHECK(create_self_signed_certificate_pkcs12(params, Asn1Blob));
}

TEST_CASE("QUIC client")
{
    SECTION("client fails to connect (no server)")
    {
        auto Client = std::make_unique<beyond_impl::MsQuicClient>(GetContext());
        auto ClientConfig = beyond_impl::MsQuicConfiguration();
        ClientConfig.DisableCertificateValidation();
        Client->Configure(ClientConfig);

        auto Observer = std::make_unique<TestQuicClient>();

        auto Connection = Client->Connect(Observer.get(), "localhost", 14284);
        
        {
            auto Future = Connection->Open();
            Future.wait();
            REQUIRE(Future.get() == QUIC_STATUS_ABORTED);
        }

        auto Lk = std::unique_lock(Observer->Mtx);
        Observer->Cv.wait_for(Lk, std::chrono::milliseconds(500), [&](){ return Observer->DisconnectEventCount == 1; });

        REQUIRE(Observer->ConnectEventCount == 0);
        REQUIRE(Observer->DisconnectEventCount == 1);
    }

}

#include <chrono>
#include <thread>

static std::vector<uint8_t> Data(1024*1024*256ll);

TEST_CASE("QUIC Client+Server")
{
    auto Server = std::make_unique<TestQuicServer>(GetContext());

    auto ServerConfig = beyond_impl::MsQuicConfiguration();
    ServerConfig.SetCertificatePKCS12({(uint8_t*)Asn1Blob.data(), Asn1Blob.size()});

    Server->Configure(ServerConfig);
    Server->Start(HOST ":19001");
    auto Port = Server->GetPort();

    LOG(INFO) << "Server listening on port " << Server->GetPort();

    auto Quic = std::make_unique<MsQuicClient>(GetContext());
    auto ClientConfig = beyond_impl::MsQuicConfiguration();
    
    ClientConfig.DisableCertificateValidation();
    Quic->Configure(ClientConfig);

    auto Client = std::make_unique<TestQuicClient>();
    auto ClientConnection = Quic->Connect(Client.get(), HOST, Port);

    {
        auto Future = ClientConnection->Open();
        Future.wait();
        auto Status = Future.get();
        REQUIRE(Status == QUIC_STATUS_SUCCESS);
    }
    auto Connected = Server->ClientConnected.get_future();

    auto ClientStream = ClientConnection->OpenStream();
    ClientStream->SetStreamHandler(Client.get());
    ClientStream->EnableRecv();
    {
        auto Future = ClientStream->Open();
        Future.wait();
        REQUIRE(Future.get() == QUIC_STATUS_SUCCESS);
    }

    SECTION("server&client, disconnect events")
    {
        Server->Stop();
        ClientConnection->Close().wait();
        Server->Connection->Close().wait();

        auto LkServer = std::unique_lock(Server->Mtx);
        Server->Cv.wait_for(LkServer, std::chrono::milliseconds(500), [&](){ return Server->DisconnectEventCount == 1; });

        REQUIRE(Server->ConnectEventCount == 1);
        REQUIRE(Server->DisconnectEventCount == 1);

        auto LkClient = std::unique_lock(Client->Mtx);
        Client->Cv.wait_for(LkServer, std::chrono::milliseconds(500), [&](){ return Client->DisconnectEventCount == 1; });

        REQUIRE(Client->ConnectEventCount == 1);
        REQUIRE(Client->DisconnectEventCount == 1);
    }

    SECTION("send/recv")
    {
        Connected.wait();
        static std::vector<nonstd::span<uint8_t>> Buffer{ 
            nonstd::span(Data.data(), Data.size())
        };

        auto start = std::chrono::high_resolution_clock::now();
        Client->Write(ClientStream.get(), {Buffer.data(), Buffer.size()}).wait();

        std::atomic_int SentTotal = 0;
        for (const auto& Buf : Buffer) { SentTotal += Buf.size(); }

        REQUIRE(Server->RecvdTotal == SentTotal);

        auto stop = std::chrono::high_resolution_clock::now();
        auto seconds = (double)std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()/(1000.0*1000.0);
        auto mbytes = Buffer.size()*(double)Data.size()/(1024*1024);
        LOG(INFO) << "transmitted " << mbytes << " MiB in " << seconds << " seconds (" << mbytes/seconds << " MiB/s (" << 8.0*mbytes/seconds<< " Mbit/s)" ;

        ClientStream->Close().wait();
        Server->Stream->Close().wait();
        ClientConnection->Close().wait();
        Server->Connection->Close().wait();
        
        // check that all events were fired
        auto LkServer = std::unique_lock(Server->Mtx);
        Server->Cv.wait_for(LkServer, std::chrono::milliseconds(1000), [&](){ return Server->DisconnectEventCount == 1; });

        REQUIRE(Server->ConnectEventCount == 1);
        REQUIRE(Server->DisconnectEventCount == 1);

        auto LkClient = std::unique_lock(Client->Mtx);
        Client->Cv.wait_for(LkClient, std::chrono::milliseconds(1000), [&](){ return Client->DisconnectEventCount == 1; });

        REQUIRE(Client->ConnectEventCount == 1);
        REQUIRE(Client->DisconnectEventCount == 1);
    }

    SECTION("send/recv + abort")
    {
        Connected.wait();
        static std::vector<nonstd::span<uint8_t>> Buffer{ 
            nonstd::span(Data.data(), Data.size())
        };
        
        auto start = std::chrono::high_resolution_clock::now();
        Client->Write(ClientStream.get(), {Buffer.data(), Buffer.size()}).wait();

        std::atomic_int SentTotal = 0;
        for (const auto& Buf : Buffer) { SentTotal += Buf.size(); }

        REQUIRE(Server->RecvdTotal == SentTotal);

        auto stop = std::chrono::high_resolution_clock::now();
        auto seconds = (double)std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count()/(1000.0*1000.0);
        auto mbytes = Buffer.size()*(double)Data.size()/(1024*1024);
        LOG(INFO) << "transmitted " << mbytes << " MiB in " << seconds << " seconds (" << mbytes/seconds << " MiB/s (" << 8.0*mbytes/seconds<< " Mbit/s)" ;
        
        Server->Stream->Abort();

        ClientStream->Close().wait();
        // do NOT call Close() after abort
        ClientConnection->Close().wait();
        Server->Connection->Close().wait();

        // check that all events were fired
        auto LkServer = std::unique_lock(Server->Mtx);
        Server->Cv.wait_for(LkServer, std::chrono::milliseconds(500), [&](){ return Server->DisconnectEventCount == 1; });

        REQUIRE(Server->ConnectEventCount == 1);
        REQUIRE(Server->DisconnectEventCount == 1);

        auto LkClient = std::unique_lock(Client->Mtx);
        Client->Cv.wait_for(LkClient, std::chrono::milliseconds(500), [&](){ return Client->DisconnectEventCount == 1; });

        REQUIRE(Client->ConnectEventCount == 1);
        REQUIRE(Client->DisconnectEventCount == 1);
    }
}
