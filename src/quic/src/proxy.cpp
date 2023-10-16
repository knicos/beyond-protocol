#include <msgpack.hpp>
#include <loguru.hpp>

#include "quic_universe_impl.hpp"

#include "proxy.hpp"

static std::atomic_int32_t MsgCtr = 0;

// A helper class for async msgpack writes (not ideal but fastest to get to work)
struct ProxyClient::Writer : public beyond_impl::IMsQuicStreamHandler
{
    struct Write
    {
        QUIC_BUFFER QuicBuffer[1];
        msgpack::sbuffer Buffer;
        std::promise<QUIC_STATUS> Promise;
    };

    std::deque<Write> mBuffers;
    std::mutex mMtx;

    std::future<QUIC_STATUS> Write(beyond_impl::MsQuicStream* Stream, msgpack::sbuffer BufferIn, bool Wait=true)
    {
        auto Lk = std::unique_lock(mMtx);
        auto& Buffer = mBuffers.emplace_back();
        Buffer.Buffer = std::move(BufferIn);
        Buffer.QuicBuffer[0].Buffer = (uint8_t*) Buffer.Buffer.data();
        Buffer.QuicBuffer[0].Length = (int32_t) Buffer.Buffer.size();
        Lk.unlock();

        Stream->Write({&(Buffer.QuicBuffer[0]), 1}, &Buffer);
        return Buffer.Promise.get_future();
    }

    void OnData(beyond_impl::MsQuicStream* Stream, nonstd::span<const QUIC_BUFFER> Data) override
    {

    }

    void OnWriteCancelled(int32_t id) override
    {
        // FIXME return context 
        auto Lk = std::unique_lock(mMtx);
        mBuffers.pop_front();
    }

    void OnWriteComplete(beyond_impl::MsQuicStream* stream, void* Context, bool Cancelled) override
    {
        auto Lk = std::unique_lock(mMtx);
        CHECK(&(mBuffers.front()) == Context);
        mBuffers.front().Promise.set_value(QUIC_STATUS_SUCCESS);
        mBuffers.pop_front();
    }
};

ProxyClient::ProxyClient(beyond_impl::QuicUniverseImpl* Universe) : mUniverse(Universe)
{
    mWriter = std::make_unique<Writer>();
}

ProxyClient::~ProxyClient() {}

void ProxyClient::SetConnection(beyond_impl::MsQuicConnectionPtr ConnectionIn) 
{
    mConnection = std::move(ConnectionIn);
    mStream = mConnection->OpenStream();
    mStream->SetStreamHandler(mWriter.get());
    mStream->EnableRecv();
}

bool ProxyClient::SetLocalName(const std::string& Name)
{
    mClientName = Name;
    auto msg = std::make_tuple(MsgCtr++, "set_name", mClientName);

    msgpack::sbuffer buffer;
    msgpack::pack(buffer, msg);
    mWriter->Write(mStream.get(), std::move(buffer), false).wait();
    return true;
}

void ProxyClient::OnConnect(beyond_impl::MsQuicConnection* Connection)
{

}

void ProxyClient::OnDisconnect(beyond_impl::MsQuicConnection* Connection)
{

}

void ProxyClient::OnStreamCreate(beyond_impl::MsQuicConnection* Connection, std::unique_ptr<beyond_impl::MsQuicStream> Stream)
{
    mUniverse->OnStreamCreate(Connection, std::move(Stream));
}

void ProxyClient::OnCertificateReceived(beyond_impl::MsQuicConnection* Connection, QUIC_BUFFER* Certificate, QUIC_BUFFER* Chain)
{

}

beyond_impl::MsQuicStreamPtr ProxyClient::OpenStream(const std::string& name)
{
    Writer Handler;

    auto msg = std::make_tuple(MsgCtr++, "connect", name);
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, msg);

    auto Stream = mConnection->OpenStream();
    Stream->SetStreamHandler(&Handler);
    auto Future = Handler.Write(Stream.get(), std::move(buffer));
    Future.wait_for(std::chrono::milliseconds(500));
    if (!Future.valid())
    {
        Stream->Close();
        Future.wait();

        LOG(ERROR) << "[Quic Proxy]: Could not open stream";
        return nullptr;
    }
    
    return Stream;
}
