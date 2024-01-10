#include <loguru.hpp>

#ifdef ENABLE_PROFILER
#include <ftl/profiler.hpp>
#endif

#include "quic.hpp"
#include "connection.hpp"
#include "stream.hpp"

#include "msquichelper.hpp"

using namespace beyond_impl;

MsQuicConnection::MsQuicConnection(IMsQuicConnectionHandler* ObserverIn, MsQuicContext* Context) :
        bClosed(false),
        MsQuic(Context),
        hConnection(nullptr),
        Observer(ObserverIn)
{
}

MsQuicConnection::~MsQuicConnection()
{
    CHECK(hConnection);
    MsQuic->Api->ConnectionClose(hConnection);
    hConnection = nullptr;
}

void MsQuicConnection::EnableStatistics()
{
    QUIC_ADDR Addr;
    uint32_t BufferLength = sizeof(Addr);

    CHECK_QUIC(MsQuic->Api->GetParam(
        hConnection,
        QUIC_PARAM_CONN_REMOTE_ADDRESS,
        &BufferLength, (void*)&Addr));
    
    QUIC_ADDR_STR AddrStrBuffer;
    QuicAddrToString(&Addr, &AddrStrBuffer);
    std::string AddrString(AddrStrBuffer.Address);
    AddrString = "[" + AddrString + "] ";
    
    // NOTE: Pointers indexed by order
    #ifdef ENABLE_PROFILER
    StatisticsPtrs.push_back(PROFILER_RUNTIME_PERSISTENT_NAME(AddrString + "Rtt"));
    TracyPlotConfig(StatisticsPtrs.back(), tracy::PlotFormatType::Number, false, true, 0);

    // Send
    StatisticsPtrs.push_back(PROFILER_RUNTIME_PERSISTENT_NAME(AddrString + "SendTotalBytes"));
    TracyPlotConfig(StatisticsPtrs.back(), tracy::PlotFormatType::Memory, false, true, 0);

    StatisticsPtrs.push_back(PROFILER_RUNTIME_PERSISTENT_NAME(AddrString + "SendCongestionCount"));
    TracyPlotConfig(StatisticsPtrs.back(), tracy::PlotFormatType::Number, false, true, 0);

    StatisticsPtrs.push_back(PROFILER_RUNTIME_PERSISTENT_NAME(AddrString + "SendSuspectedLostPackets"));
    TracyPlotConfig(StatisticsPtrs.back(), tracy::PlotFormatType::Number, false, true, 0);

    StatisticsPtrs.push_back(PROFILER_RUNTIME_PERSISTENT_NAME(AddrString + "SendSpuriousLostPackets"));
    TracyPlotConfig(StatisticsPtrs.back(), tracy::PlotFormatType::Number, false, true, 0);

    StatisticsPtrs.push_back(PROFILER_RUNTIME_PERSISTENT_NAME(AddrString + "SendCongestionWindow"));
    TracyPlotConfig(StatisticsPtrs.back(), tracy::PlotFormatType::Memory, false, true, 0);

    // Recv
    StatisticsPtrs.push_back(PROFILER_RUNTIME_PERSISTENT_NAME(AddrString + "RecvTotalBytes"));
    TracyPlotConfig(StatisticsPtrs.back(), tracy::PlotFormatType::Memory, false, true, 0);

    StatisticsPtrs.push_back(PROFILER_RUNTIME_PERSISTENT_NAME(AddrString + "RecvDroppedPackets"));
    TracyPlotConfig(StatisticsPtrs.back(), tracy::PlotFormatType::Number, false, true, 0);

    CHECK(StatisticsPtrs.size() == 8);
    for (const auto* Ptr : StatisticsPtrs) { CHECK(Ptr); }
    #endif
}

MsQuicConnectionPtr MsQuicConnection::Accept(MsQuicContext* MsQuic, HQUIC hConfiguration, HQUIC hConnection)
{
    auto Connection = MsQuicConnectionPtr(new MsQuicConnection(nullptr, MsQuic));
    Connection->Self = Connection;

    Connection->hConnection = hConnection;

    MsQuic->Api->SetCallbackHandler(
        hConnection,
        (void*)(MsQuicConnection::EventHandler),
        Connection.get());
    
    CHECK_QUIC(MsQuic->Api->ConnectionSetConfiguration(
        hConnection,
        hConfiguration
    ));

    return Connection;
}

MsQuicConnectionPtr MsQuicConnection::Connect(IMsQuicConnectionHandler* ObserverIn, MsQuicContext* MsQuic, HQUIC hConfiguration, const std::string& Host, uint16_t Port)
{
    auto Connection = MsQuicConnectionPtr(new MsQuicConnection(ObserverIn, MsQuic));
    Connection->Self = Connection;

    CHECK_QUIC(MsQuic->Api->ConnectionOpen(
        MsQuic->hRegistration,
        MsQuicConnection::EventHandler,
        Connection.get(),
        &(Connection->hConnection)
    ));

    CHECK_QUIC(MsQuic->Api->ConnectionStart(
        Connection->hConnection,
        hConfiguration,
        QUIC_ADDRESS_FAMILY_UNSPEC,
        Host.c_str(),
        Port)
    );

    return Connection;
}

void MsQuicConnection::SetConnectionObserver(IMsQuicConnectionHandler* ObserverIn)
{
    // In principle observer could be replaced if necessary; not tested (expect bugs)
    CHECK(Observer == nullptr) << "Observer already set";
    Observer = ObserverIn;
}

MsQuicStreamPtr MsQuicConnection::OpenStream()
{
    return MsQuicStream::Create(MsQuic, hConnection);
}

void MsQuicConnection::Close()
{
    if (!bClosed.exchange(true))
    {
        MsQuic->Api->ConnectionShutdown(
            hConnection,
            QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
            0
        );
    }
}

QUIC_STATISTICS_V2 MsQuicConnection::Statistics()
{
    QUIC_STATISTICS_V2 Stats;
    uint32_t StatsSize = sizeof(Stats);
    CHECK_QUIC(MsQuic->Api->GetParam(
        hConnection,
        QUIC_PARAM_CONN_STATISTICS_V2,
        &StatsSize,
        &Stats));

    //int64_t LostPackets = (int64_t)Stats.SendSuspectedLostPackets - (int64_t)Stats.SendSpuriousLostPackets;

    #ifdef ENABLE_PROFILER
    if (StatisticsPtrs.size() > 0)
    {
        TracyPlot(StatisticsPtrs[0], (int64_t)Stats.Rtt);
        TracyPlot(StatisticsPtrs[1], (int64_t)Stats.SendTotalBytes);
        TracyPlot(StatisticsPtrs[2], (int64_t)Stats.SendCongestionCount);
        TracyPlot(StatisticsPtrs[3], (int64_t)Stats.SendSuspectedLostPackets);
        TracyPlot(StatisticsPtrs[4], (int64_t)Stats.SendSpuriousLostPackets);
        TracyPlot(StatisticsPtrs[5], (int64_t)Stats.SendCongestionWindow);
        TracyPlot(StatisticsPtrs[6], (int64_t)Stats.RecvTotalBytes);
        TracyPlot(StatisticsPtrs[7], (int64_t)Stats.RecvDroppedPackets);
    }
    #endif

    return Stats;
}

QUIC_STATUS MsQuicConnection::EventHandler(HQUIC hConnection, void* Context, QUIC_CONNECTION_EVENT* Event)
{
    MsQuicConnection* Connection  = static_cast<MsQuicConnection*>(Context);
    auto* MsQuic = Connection->MsQuic;
    auto& Observer = Connection->Observer;

    switch (Event->Type)
    {
        case QUIC_CONNECTION_EVENT_CONNECTED:
        {
            Connection->EnableStatistics();

            if (Observer)
            {
                Observer->OnConnect(Connection);
                return QUIC_STATUS_SUCCESS;
            }
            else
            {
                LOG(WARNING) << "[QUIC] SetObserver() was not called; Connection aborted (BUG; Possibly undefined behavior)";
                if (!Connection->bClosed.exchange(true))
                {
                    MsQuic->Api->ConnectionShutdown(
                        Connection->hConnection,
                        QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT,
                        0);
                }
                return QUIC_STATUS_SUCCESS;
            }
        }

        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        {
            return QUIC_STATUS_SUCCESS;
        }
        
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
        {
            return QUIC_STATUS_SUCCESS;
        }
        
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
        {
            Observer->OnDisconnect(Connection);
            Connection->Self.reset();

            return QUIC_STATUS_SUCCESS;
        }
        
        case QUIC_CONNECTION_EVENT_LOCAL_ADDRESS_CHANGED:
        {
            LOG(WARNING) << "msquic: unhandled " << QuicToString(Event->Type);
            return QUIC_STATUS_SUCCESS;
        }
        
        case QUIC_CONNECTION_EVENT_PEER_ADDRESS_CHANGED:
        {
            LOG(WARNING) << "msquic: unhandled " << QuicToString(Event->Type);
            return QUIC_STATUS_SUCCESS;
        }
        
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
        {
            HQUIC hStream = Event->PEER_STREAM_STARTED.Stream;
            auto Flags = Event->PEER_STREAM_STARTED.Flags;
            CHECK(!(Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL)) << "[QUIC] Unidirectional streams not supported";
            
            auto Stream = MsQuicStream::FromRemotePeer(MsQuic, hStream);
            Observer->OnStreamCreate(Connection, std::move(Stream));
            
            return QUIC_STATUS_SUCCESS;
        }
        
        case QUIC_CONNECTION_EVENT_STREAMS_AVAILABLE:
        {
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_PEER_NEEDS_STREAMS:
        {
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_IDEAL_PROCESSOR_CHANGED:
        {
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
        {
            // Event->DATAGRAM_STATE_CHANGED.SendEnabled;
            // Event->DATAGRAM_STATE_CHANGED.MaxSendLength;
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
        {
            //UNIQUE_LOCK_N(Lock, Connection->DatagramMutex);
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
        {
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_RESUMED:
        {
            LOG(WARNING) << "msquic: unhandled " << QuicToString(Event->Type);
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_RESUMPTION_TICKET_RECEIVED:
        {
            LOG(WARNING) << "msquic: unhandled " << QuicToString(Event->Type);
            return QUIC_STATUS_SUCCESS;
        }

        case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED:
        {
            Observer->OnCertificateReceived(
                Connection,
                (QUIC_BUFFER*)Event->PEER_CERTIFICATE_RECEIVED.Certificate,
                (QUIC_BUFFER*)Event->PEER_CERTIFICATE_RECEIVED.Chain);
            return QUIC_STATUS_SUCCESS;
        }
    }
    // should assert/error here?
    return QUIC_STATUS_SUCCESS;
}

void MsQuicConnection::ShutdownDatagrams()
{

}
