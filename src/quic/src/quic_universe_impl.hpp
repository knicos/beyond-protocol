#pragma once 

#include "../../universe.hpp"
#include "quic_universe.hpp"


namespace beyond_impl
{

class QuicUniverseImpl : public ftl::net::QuicUniverse, private IMsQuicServerConnectionHandler 
{
public:
    QuicUniverseImpl(ftl::net::Universe* net);
    ~QuicUniverseImpl() override;

    void Configure() override;

    bool CanOpenUri(const ftl::URI& uri) override;

    bool Listen(const ftl::URI& uri) override;

    std::vector<ftl::URI> GetListeningUris() override;

    ftl::net::PeerPtr Connect(const ftl::URI& uri, bool) override;

private:
    void OnConnection(MsQuicServer* Listener, MsQuicConnectionPtr Connection) override;
    
    ftl::net::Universe* net_;

    MsQuicConfiguration ClientConfig;
    bool IsStarted;

    DECLARE_MUTEX(ClientMtx);
    std::unique_ptr<MsQuicClient> Client;
    std::vector<std::unique_ptr<MsQuicServer>> Listeners;

    DECLARE_MUTEX(PeerMtx);
    std::vector<ftl::net::PeerPtr> Peers;
};

}
