#pragma once

#include <functional>

#include "quic.hpp"
#include "connection.hpp"
#include "stream.hpp"

namespace beyond_impl
{

/** */
class IMsQuicConnectionHandler
{
public:
    virtual void OnConnect(MsQuicConnection* Connection) {}
    virtual void OnDisconnect(MsQuicConnection* Connection) {}
    virtual void OnStreamCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicStream> Stream) {}
    //virtual void OnDatagramCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicDatagram> Stream) {}
    virtual void OnCertificateReceived(MsQuicConnection* Connection, QUIC_BUFFER* Certificate, QUIC_BUFFER* Chain) {}

    virtual ~IMsQuicConnectionHandler() {}
};

/** MsQuicConnection */
class MsQuicConnection : public MsQuicOpenable
{
public:
    static MsQuicConnectionPtr Accept(MsQuicContext* MsQuic, HQUIC hConfiguration, HQUIC hConnection);
    static MsQuicConnectionPtr Connect(IMsQuicConnectionHandler* Observer, MsQuicContext* MsQuic, HQUIC hConfiguration, const std::string& Host, uint16_t Port);
    
    ~MsQuicConnection();

    std::future<QUIC_STATUS> Close();

    /** Open new QUIC stream. Can be called anytime (MsQuic will queue the stream if connection is not ready yet). */
    MsQuicStreamPtr OpenStream();

    /** Open datagram channel 
     *  Not implemented. Need methods to query allowed datagram sizes. Actual datagrams should contain unique id per
     *  "channel" for similar API as stream (multiplex multiple datagram connections in single quic/udp connection)
     */
    // MsQuicDatagramPtr OpenDatagramChannel();

    /** Close given datagram channel (used by ~MsQuicDatagram())*/
    void CloseDatagramChannel(MsQuicDatagram* Ptr);

    QUIC_STATISTICS_V2 Statistics();

    MsQuicConnection(const MsQuicConnection&) = delete;
    MsQuicConnection& operator=(const MsQuicConnection&) = delete;

    // Set observer (must be set only once, either in Accept/Connect or here)
    void SetConnectionObserver(IMsQuicConnectionHandler* Observer);

private:
    MsQuicConnection(IMsQuicConnectionHandler* Observer, MsQuicContext* Context);

    MsQuicContext* MsQuic;
    HQUIC hConnection;
    IMsQuicConnectionHandler* Observer;

    // stop all datagram processing
    void ShutdownDatagrams();

    void EnableStatistics();
    std::vector<const char*> StatisticsPtrs;

    static QUIC_STATUS EventHandler(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);
};

}
