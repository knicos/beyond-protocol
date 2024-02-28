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
    /** After succesfully connected */
    virtual void OnConnect(MsQuicConnection* Connection) {}
    
    /** On disconnect/failed connect */
    virtual void OnDisconnect(MsQuicConnection* Connection) {}

    virtual void OnStreamCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicStream> Stream) {}
    //virtual void OnDatagramCreate(MsQuicConnection* Connection, std::unique_ptr<MsQuicDatagram> Stream) {}
    virtual void OnCertificateReceived(MsQuicConnection* Connection, QUIC_BUFFER* Certificate, QUIC_BUFFER* Chain) {}

    virtual ~IMsQuicConnectionHandler() {}
};

/** MsQuicConnection */
class MsQuicConnection
{
private:
    std::atomic_bool bClosed;

public:
    static MsQuicConnectionPtr Accept(MsQuicContext* MsQuic, HQUIC hConfiguration, HQUIC hConnection);
    static MsQuicConnectionPtr Connect(IMsQuicConnectionHandler* Observer, MsQuicContext* MsQuic, HQUIC hConfiguration, const std::string& Host, uint16_t Port);

    ~MsQuicConnection();

    void Close();

    /** Open new QUIC stream. Can be called anytime (MsQuic will queue the stream if connection is not ready yet). */
    MsQuicStreamPtr OpenStream();

    /** Open datagram channel 
     *  Not implemented. Need methods to query allowed datagram sizes. Actual datagrams should contain unique id per
     *  "channel" for similar API as stream (multiplex multiple datagram connections in single quic/udp connection)
    
    MsQuicDatagramPtr OpenDatagramChannel();
    */

    const QUIC_STATISTICS_V2& GetStatistics();
    void RefreshStatistics();

    int32_t GetRtt() const { return Rtt; }

    MsQuicConnection(const MsQuicConnection&) = delete;
    MsQuicConnection& operator=(const MsQuicConnection&) = delete;

    // Set observer (must be set only once, either in Accept/Connect or here)
    void SetConnectionObserver(IMsQuicConnectionHandler* Observer);

private:
    void ProfilerLogStatistics(const QUIC_STATISTICS_V2&, int64_t);

    MsQuicConnection(IMsQuicConnectionHandler* Observer, MsQuicContext* Context);

    MsQuicContext* MsQuic;
    HQUIC hConnection;
    IMsQuicConnectionHandler* Observer;

    // After QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE event is received, the shared_ptr is cleared and the destructor
    // may release the handle. Perhaps not ideal but avoids having another layer of abstraction between this and msquic.
    MsQuicConnectionPtr Self;

    // stop all datagram processing
    void ShutdownDatagrams();

    void EnableStatistics();

    QUIC_STATISTICS_V2 Statistics;
    
    QUIC_STATISTICS_V2 StatisticsPrev;
    int64_t StatisticsTimePrev = 0;

    std::atomic_int32_t Rtt;

    std::array<const char*, 8> StatisticsIdPtrs;

    static QUIC_STATUS EventHandler(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event);
};

}
