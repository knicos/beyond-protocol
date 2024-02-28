#include "msquic/quic.hpp"
#include "quic_universe_impl.hpp"
#include "quic_peer.hpp"

#include "openssl_util.hpp"

using namespace ftl::net;
using namespace beyond_impl;

static std::mutex MsQuicMtx_;
static int MsQuicCtr_ = 0;
static MsQuicContext MsQuic;

void QuicUniverse::Unload(bool force)
{
    auto lk = std::unique_lock(MsQuicMtx_);
    if (MsQuic.IsValid() && (force || (MsQuicCtr_ == 0)))
    {
        LOG_IF(WARNING, MsQuicCtr_ != 0) << "[QUIC] Unloading MsQuic before all users have released their resources";
        MsQuicContext::Close(MsQuic);
    }
}

std::unique_ptr<ftl::net::QuicUniverse> QuicUniverse::Create(Universe* net)
{
    return std::make_unique<QuicUniverseImpl>(net);
}

QuicUniverseImpl::QuicUniverseImpl(Universe* net) : net_(net ), IsClosing(false)
{
    auto lk = std::unique_lock(MsQuicMtx_);
    if (MsQuicCtr_++ == 0)
    {
        MsQuicContext::Open(MsQuic, "Beyond");
        CHECK(MsQuic.IsValid());
    }

    ClientConfig.DisableCertificateValidation(); // FIXME!!
    // Keeps connection alive when idle (only for client)
    ClientConfig.SetKeepAlive(100); // TODO: Config value? 
    Client = std::make_unique<MsQuicClient>(&MsQuic);
    Client->Configure(ClientConfig);
}

QuicUniverseImpl::~QuicUniverseImpl()
{
    {
        UNIQUE_LOCK_N(Lk, ClientMtx);
        Listeners.clear();
    }
    
    {
        UNIQUE_LOCK_N(Lk, ConnectionMtx);
        IsClosing = true;
        for (auto& Connection : Connections)
        {
            Connection->Close();
        }

        // Must wait for all connections to clear
        ConnectionCv.wait(Lk, [&]() -> bool { return Connections.size() == 0; });
    }

    {
        auto lk = std::unique_lock(ClientMtx);
        Client.reset();
    }
    {
        auto lk = std::unique_lock(MsQuicMtx_);
        --MsQuicCtr_;
    }

    Unload(false);
}

void QuicUniverseImpl::Configure()
{
    // TODO: perhaps accept nlohmann json and extract parameters from there 
}

void QuicUniverseImpl::Shutdown()
{
    Proxy.reset();
}

bool QuicUniverseImpl::CanOpenUri(const ftl::URI& uri)
{
    return (
          (uri.getScheme() == ftl::URI::scheme_t::SCHEME_FTL_QUIC)
        || (uri.getScheme() == ftl::URI::scheme_t::SCHEME_PROXY)
    );
}

bool QuicUniverseImpl::Listen(const ftl::URI& uri)
{
    UNIQUE_LOCK_N(lk, ClientMtx);
    auto Server = std::make_unique<MsQuicServer>(&MsQuic);

    auto Config = MsQuicConfiguration();

    // FIXME: define in config file
    CertificateParams CertParams;
    std::vector<uint8_t> CertBlob;
    create_self_signed_certificate_pkcs12(CertParams, CertBlob);

    Config.SetCertificatePKCS12(CertBlob);

    Server->Configure(Config);
    Server->SetCallbackHandler(this);

    int port = uri.getPort();
    std::string addr = uri.getHost();
    if (port != 0) { addr += ":" + std::to_string(port); }
    Server->Start(addr);
    Listeners.push_back(std::move(Server));
    
    return true;
}

std::vector<ftl::URI> QuicUniverseImpl::GetListeningUris()
{
    LOG(ERROR) << "NOT IMPLEMENTED: " << __func__;
    return {};
}

PeerPtr QuicUniverseImpl::Connect(const ftl::URI& uri, bool is_webservice)
{
    UNIQUE_LOCK_N(Lk, ConnectionMtx);
    if (IsClosing)
    {
        LOG(WARNING) << "[QUIC] Connect: Universe closing";
        return nullptr;
    }

    if (uri.getScheme() == ftl::URI::SCHEME_PROXY) {
        if (!Proxy) {
            LOG(ERROR) << "[QUIC] Proxy connection requested, but no proxy configured";
            return nullptr;
        }
        auto Stream = Proxy->OpenStream(uri.getHost());
        if (Stream) {
            LOG(INFO) << "[QUIC] Stream opened (via proxy): " << uri.to_string();
            return std::make_unique<QuicPeerStream>(nullptr, std::move(Stream), net_, net_->dispatcher_());
        } else {
            LOG(ERROR) << "[QUIC] Could not open stream via proxy: " << uri.to_string();
            return nullptr;
        }

    } else if (uri.getScheme() == ftl::URI::SCHEME_FTL_QUIC) {
        LOG(INFO) << "[QUIC] Connecting to: " << uri.to_string() << (is_webservice ? " (webservice)" : "");
        auto Connection = Client->Connect(this, uri.getHost(), uri.getPort());
        auto Stream = Connection->OpenStream();
        auto Ptr = std::make_unique<QuicPeerStream>(Connection.get(), std::move(Stream), net_, net_->dispatcher_());
        if (is_webservice) { Ptr->set_type(ftl::protocol::NodeType::kWebService); }
        // Expected to be called by net::Universe, which will insert peer to its internal list
        CHECK(Ptr->getType() == (is_webservice ? ftl::protocol::NodeType::kWebService : ftl::protocol::NodeType::kNode));
        Connections.push_back(std::move(Connection));
        return Ptr;

    } else {
        LOG(ERROR) << "[QUIC] Unknown url (BUG)";
    }

    return nullptr;
}

void QuicUniverseImpl::ConnectProxy(const ftl::URI& url) 
{
    if (!Proxy) {
        // TODO: get password from uri and add simple password login
        Proxy = std::make_unique<ProxyClient>(this);
        Proxy->SetConnection(Client->Connect(Proxy.get(), url.getHost(), url.getPort()));
        Proxy->SetLocalName(url.getUserInfo());
    }
    else {
        LOG(ERROR) << "Quic proxy already configured";
    }
}

void QuicUniverseImpl::OnConnection(MsQuicServer* Listener, MsQuicConnectionPtr Connection, const QUIC_NEW_CONNECTION_INFO& Info)
{
    UNIQUE_LOCK_N(Lk, ConnectionMtx);
    if (IsClosing) { return; }

    Connection->SetConnectionObserver(this);

    QUIC_ADDR_STR AddrStr;
    QuicAddrToString(Info.RemoteAddress, &AddrStr);

    LOG(INFO) << "[QUIC] New incoming connection from " << AddrStr.Address << " (total: " << (Connections.size() + 1) << ")"; 

    Connections.push_back(std::move(Connection));
}

void QuicUniverseImpl::OnConnect(MsQuicConnection* Connection)
{
    ConnectionCv.notify_all();
}

void QuicUniverseImpl::OnDisconnect(MsQuicConnection* Connection)
{
    UNIQUE_LOCK_N(Lk, ConnectionMtx);
    Connections.erase(std::remove_if(Connections.begin(), Connections.end(), 
        [&](const auto& Ptr){ return (Ptr.get() == Connection); }));
    
    LOG(INFO) << "[QUIC] Client disconnected (total: " << Connections.size() << ")"; 
    Lk.unlock();

    ConnectionCv.notify_all();
}

void QuicUniverseImpl::OnStreamCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicStream> Stream)
{
    auto Peer = std::make_shared<QuicPeerStream>(Connection, std::move(Stream), net_, net_->dispatcher_()); 
    net_->insertPeer_(Peer);
    Peer->send_handshake();
    Peer->start();
}
