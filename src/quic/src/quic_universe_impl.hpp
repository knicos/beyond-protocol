#pragma once 

#include "../../universe.hpp"
#include "quic_universe.hpp"

#include "msquic/connection.hpp"

#include "proxy.hpp"

namespace beyond_impl
{

class QuicUniverseImpl : public ftl::net::QuicUniverse, private IMsQuicServerConnectionHandler, public IMsQuicConnectionHandler
{
public:
    QuicUniverseImpl(ftl::net::Universe* net);
    ~QuicUniverseImpl() override;

    void Configure() override;

    bool CanOpenUri(const ftl::URI& uri) override;

    bool Listen(const ftl::URI& uri) override;

    std::vector<ftl::URI> GetListeningUris() override;

    ftl::net::PeerPtr Connect(const ftl::URI& uri, bool) override;

    void ConnectProxy(const ftl::URI& url) override;

    void Shutdown() override;

    void OnConnect(MsQuicConnection* Connection) override;
    void OnDisconnect(MsQuicConnection* Connection) override;
    void OnStreamCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicStream> Stream) override;

    void OnConnection(MsQuicServer* Listener, MsQuicConnectionPtr Connection, const QUIC_NEW_CONNECTION_INFO& Info) override;
    
    ftl::net::Universe* net_;

    MsQuicConfiguration ClientConfig;
    bool IsClosing;

    DECLARE_MUTEX(ClientMtx);
    std::unique_ptr<MsQuicClient> Client;
    std::vector<std::unique_ptr<MsQuicServer>> Listeners;

    DECLARE_MUTEX(ConnectionMtx);
    std::vector<MsQuicConnectionPtr> Connections;
    std::condition_variable_any ConnectionCv;

    std::unique_ptr<ProxyClient> Proxy;
};

}
