#pragma once

#include <msgpack.hpp>

#include "msquic/connection.hpp"
#include "msquic/stream.hpp"

namespace beyond_impl
{
    class QuicUniverseImpl;
}

class ProxyClient final : public beyond_impl::IMsQuicConnectionHandler, private beyond_impl::IMsQuicStreamHandler
{
public:
    ProxyClient(beyond_impl::QuicUniverseImpl* Universe);
    ~ProxyClient();

    void SetConnection(beyond_impl::MsQuicConnectionPtr ConnectionIn) ;
    beyond_impl::MsQuicConnection* ConnectionHandle() { return mConnection.get(); };

    beyond_impl::MsQuicStreamPtr OpenStream(const std::string& name);

    bool SetLocalName(const std::string& Name);

private:
    beyond_impl::MsQuicConnectionPtr mConnection;
    beyond_impl::MsQuicStreamPtr mStream;
    beyond_impl::QuicUniverseImpl* mUniverse;
    std::string mClientName;

    struct Writer;
    std::unique_ptr<Writer> mWriter;
    
    void OnConnect(beyond_impl::MsQuicConnection* Connection) override;
    void OnDisconnect(beyond_impl::MsQuicConnection* Connection) override;
    void OnStreamCreate(beyond_impl::MsQuicConnection* Connection, std::unique_ptr<beyond_impl::MsQuicStream> Stream) override;

    void OnCertificateReceived(beyond_impl::MsQuicConnection* Connection, QUIC_BUFFER* Certificate, QUIC_BUFFER* Chain) override;
};
