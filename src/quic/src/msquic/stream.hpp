#pragma once

#include <atomic>

#include "quic.hpp"

namespace beyond_impl
{

class IMsQuicStreamHandler
{
public:
    virtual ~IMsQuicStreamHandler() = default;

    /** Read callback when data is available (asynchronous callback).
     *  Callback may not do any expensive processing. Copying the buffers and
     *  calling Consume() is possible, (expenisve) decoding etc should be done
     *  in different thread.
     */
    virtual void OnData(MsQuicStream* stream, nonstd::span<const QUIC_BUFFER> data) {}

    virtual void OnWriteCancelled(int32_t id) {}

    /**  */
    virtual void OnWriteComplete(MsQuicStream* stream, void* Context, bool Cancelled) {}

    virtual void OnShutdown(MsQuicStream* stream) {}

    virtual void OnShutdownComplete(MsQuicStream* stream) {}
};

class MsQuicStream : public MsQuicOpenable
{
public:
    using WriteCallback_t = std::function<void(void*, bool)>;

    ~MsQuicStream();

    /** Created by remote */
    static MsQuicStreamPtr FromRemotePeer(MsQuicContext* MsQuic,  HQUIC hStream);

    /** Create local */
    static MsQuicStreamPtr Create(MsQuicContext* MsQuic, HQUIC hConnection);

    /** Immidiately shut down the stream, call OnShutdownComplete and close the stream. Blocks until
     *  all SHUTDOWN_COMPLETE event is received (and OnShutdownComplete callback returns).
    */
    void Abort();

    /** Inform source that reader is done with data. When less than received is consumed, .
     */
    void Consume(int32_t BytesConsumed);

    void EnableRecv(bool Value=true);

    /** MsQuic Send. If Delay flag set to true, the flag is passed to MsQuic (indicate more data queued shortly, 
     *  might delay transmission indefinitely if no data is passed without the flag?). Returns true on succesfully
     *  queued data. */
    bool Write(nonstd::span<const QUIC_BUFFER> Buffers, void* Context, bool Delay = false);

    void SetStreamHandler(IMsQuicStreamHandler*);

    /** Calls to Start() */
    std::future<QUIC_STATUS> Open();

    /** Calls to Stop(), Stream must not be used after the call. */
    std::future<QUIC_STATUS> Close();

private:
    MsQuicStream(MsQuicContext* MsQuic);

    MsQuicContext* MsQuic;
    // Should be released only in destructor, so writes will return with correct error code (and to avoid write
    // races during StreamClose).
    HQUIC hStream;

    IMsQuicStreamHandler* Observer;

    std::atomic_int PendingSends;

    static QUIC_STATUS EventHandler(HQUIC Connection, void* Context, QUIC_STREAM_EVENT* Event);
};

}
