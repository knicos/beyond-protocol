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
    UNIQUE_LOCK_N(Lk, MsQuicMtx_);
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

QuicUniverseImpl::QuicUniverseImpl(Universe* net) : net_(net )
{
    UNIQUE_LOCK_N(Lk, MsQuicMtx_);
    if (MsQuicCtr_++ == 0)
    {
        MsQuicContext::Open(MsQuic, "Beyond");
        CHECK(MsQuic.IsValid());
    }

    ClientConfig.DisableCertificateValidation(); // FIXME!!
    Client = std::make_unique<MsQuicClient>(&MsQuic);
    Client->Configure(ClientConfig);
}

QuicUniverseImpl::~QuicUniverseImpl()
{
    {
        Listeners.clear();
    }
    {
        UNIQUE_LOCK_N(Lk, PeerMtx);
        while(Peers.size() > 0)
        {
            Peers.back()->close();
            Peers.pop_back();
        }
    }
    {
        UNIQUE_LOCK_N(Lk, ClientMtx);
        Client.reset();
    }
    {
        UNIQUE_LOCK_N(Lk, MsQuicMtx_);
        --MsQuicCtr_;
    }
}

void QuicUniverseImpl::Configure()
{
    // TODO: perhaps accept nlohmann json and extract parameters from there 
}

bool QuicUniverseImpl::CanOpenUri(const ftl::URI& uri)
{
    return uri.getScheme() == ftl::URI::scheme_t::SCHEME_FTL_QUIC;
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
    return {};
}

PeerPtr QuicUniverseImpl::Connect(const ftl::URI& uri)
{
    LOG(INFO) << "[QUIC] Connecting to: " << uri.to_string();
    auto Peer = std::make_shared<QuicPeer>(&MsQuic, net_, net_->dispatcher_());
    auto Connection = Client->Connect(Peer.get(), uri.getHost(), uri.getPort());
    Peer->set_connection(std::move(Connection));

    UNIQUE_LOCK_N(Lk, PeerMtx);
    Peers.push_back(Peer);

    return Peer;
}

void QuicUniverseImpl::OnConnection(MsQuicServer* Listener, MsQuicConnectionPtr Connection)
{
    auto Peer = std::make_shared<QuicPeer>(&MsQuic, net_, net_->dispatcher_());
    Connection->SetConnectionObserver(Peer.get());
    Peer->set_connection(std::move(Connection));

    LOG(INFO) << "[QUIC] New incoming connection";

    net_->insertPeer_(Peer);

    Peer->start();
    Peer->initiate_handshake();

    UNIQUE_LOCK_N(Lk, PeerMtx);
    Peers.push_back(Peer);
}
