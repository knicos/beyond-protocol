#include "stream.hpp"
#include "connection.hpp"

#include <array>

#include <loguru.hpp>

#include "msquichelper.hpp"

using namespace beyond_impl;


std::unique_ptr<MsQuicStream> MsQuicStream::FromRemotePeer(MsQuicContext* MsQuic, HQUIC hStream)
{
    CHECK(MsQuic);
    CHECK(hStream);

    auto Stream = std::unique_ptr<MsQuicStream>(new MsQuicStream(MsQuic));
    CHECK(Stream);

    Stream->hStream = hStream;

    CHECK_QUIC(MsQuic->Api->StreamReceiveSetEnabled(hStream, false));

    MsQuic->Api->SetCallbackHandler(
        hStream,
        (void*)(MsQuicStream::EventHandler),
        Stream.get());

    Stream->bOpen = true;

    return Stream;
}

std::unique_ptr<MsQuicStream> MsQuicStream::Create(MsQuicContext* MsQuic, HQUIC hConnection)
{
    CHECK(MsQuic);
    CHECK(hConnection);
    
    auto Stream = std::unique_ptr<MsQuicStream>(new MsQuicStream(MsQuic));
    CHECK(Stream);

    CHECK_QUIC(MsQuic->Api->StreamOpen(
        hConnection,
        QUIC_STREAM_OPEN_FLAG_NONE,
        MsQuicStream::EventHandler,
        Stream.get(),
        &(Stream->hStream)
    ));
    
    CHECK_QUIC(MsQuic->Api->StreamReceiveSetEnabled(Stream->hStream, false));

    CHECK_QUIC(MsQuic->Api->StreamStart(
        Stream->hStream, QUIC_STREAM_START_FLAG_IMMEDIATE));

    Stream->bOpen = true;

    return Stream;
}

QUIC_STATUS MsQuicStream::EventHandler(HQUIC hStream, void* Context, QUIC_STREAM_EVENT* Event)
{
    MsQuicStream* Stream = static_cast<MsQuicStream*>(Context);
    auto* MsQuic = Stream->MsQuic;

    switch (Event->Type)
    {
    case QUIC_STREAM_EVENT_START_COMPLETE:
    {
        Stream->SetOpenStatus(QUIC_STATUS_SUCCESS);
        return QUIC_STATUS_SUCCESS;
    }

    case QUIC_STREAM_EVENT_RECEIVE:
    {
        if (Stream->Observer == nullptr)
        {
            // Disable further receive callbacks (can be re-enabled after observer is added).
            Event->RECEIVE.TotalBufferLength = 0;
            return QUIC_STATUS_SUCCESS;
        }

        const auto Buffers = Event->RECEIVE.Buffers;
        const auto BufferCount = Event->RECEIVE.BufferCount;

        if (BufferCount == 0)
        {
            // When the buffer count is 0, it signifies the reception of a QUIC frame with empty data,
            // which also indicates the end of stream data.
            return QUIC_STATUS_SUCCESS;
        }

        Stream->Observer->OnData(Stream, nonstd::span{Buffers, BufferCount});
        return QUIC_STATUS_PENDING;
    }

    case QUIC_STREAM_EVENT_SEND_COMPLETE:
    {
        Stream->Observer->OnWriteComplete(Stream, Event->SEND_COMPLETE.ClientContext, Event->SEND_COMPLETE.Canceled);
        return QUIC_STATUS_SUCCESS;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
    {
        CHECK_QUIC(MsQuic->Api->StreamShutdown(
            Stream->hStream,
            QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL,
            0));
        
        return QUIC_STATUS_SUCCESS;
    }

    case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
    {
        break;
    }

    case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
    {
        break;
    }

    case QUIC_STREAM_EVENT_SEND_SHUTDOWN_COMPLETE:
    {
        return QUIC_STATUS_SUCCESS;
    }

    case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
    {
        if (!Stream->IsOpen())
        {
            Stream->SetOpenStatus(QUIC_STATUS_ABORTED);
            Stream->SetCloseStatus(QUIC_STATUS_ABORTED);
        }
        else
        {
            Stream->SetCloseStatus(QUIC_STATUS_SUCCESS);
        }
        // Stream handle closed in destructor (attempts to write will result in error instead of undefined behavior)
        Stream->Observer->OnShutdownComplete(Stream);
        return QUIC_STATUS_SUCCESS;
    }

    case QUIC_STREAM_EVENT_IDEAL_SEND_BUFFER_SIZE:
    {
        return QUIC_STATUS_SUCCESS;
    }

    case QUIC_STREAM_EVENT_PEER_ACCEPTED:
    {
        LOG(WARNING) << "msquic: unhandled " << QuicToString(Event->Type);
        return QUIC_STATUS_SUCCESS;
    }
    } // switch

    LOG(ERROR) << "[QUIC] Unhandled " << QuicToString(Event->Type);
    return QUIC_STATUS_SUCCESS;
}

MsQuicStream::MsQuicStream(MsQuicContext* MsQuicIn) : 
    MsQuic(MsQuicIn), hStream(nullptr), Observer(nullptr), PendingSends(0)
{
}

MsQuicStream::~MsQuicStream() 
{
    if (IsOpen())
    {
        // SHUTDOWN_COMPLETE has not been received, stream must be closed immediately
        LOG(WARNING) << "[QUIC] Abort() called in MsQuicStream destructor (SHUTDOWN_COMPLETE has not been received)";
        Abort();
    }

    if (hStream)
    {
        MsQuic->Api->StreamClose(hStream);
        hStream = nullptr;
    }

    CHECK(!hStream && !IsOpen()) << "SHUTDOWN_COMPLETE was not received but callback handler destroyed!";
}

std::future<QUIC_STATUS> MsQuicStream::Open() 
{
    return MsQuicOpenable::Open();
}

std::future<QUIC_STATUS> MsQuicStream::Close() 
{
    CHECK(MsQuic);
    CHECK(hStream);
    CHECK_QUIC(MsQuic->Api->StreamShutdown(
        hStream,
        QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL,
        0
    ));
    return MsQuicOpenable::Close();
}

void MsQuicStream::Abort()
{
    if (hStream)
    {
        CHECK_QUIC(MsQuic->Api->StreamShutdown(hStream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT|QUIC_STREAM_SHUTDOWN_FLAG_IMMEDIATE, 0));
        MsQuicOpenable::Close().wait();
    }
}

void MsQuicStream::Consume(int32_t BytesConsumed)
{
    CHECK(MsQuic);
    CHECK(hStream);

    MsQuic->Api->StreamReceiveComplete(hStream, BytesConsumed);
}

void MsQuicStream::EnableRecv(bool Value)
{
    CHECK(MsQuic);
    CHECK(hStream);

    CHECK_QUIC(MsQuic->Api->StreamReceiveSetEnabled(hStream, Value));
}

bool MsQuicStream::Write(nonstd::span<const QUIC_BUFFER> Buffers, void* Context, bool Delay)
{
    QUIC_SEND_FLAGS Flags = QUIC_SEND_FLAG_NONE;
    if (Delay) { Flags |= QUIC_SEND_FLAG_DELAY_SEND; }

    CHECK(MsQuic);
    
    if (!hStream) { return false; }

    return QUIC_SUCCEEDED(MsQuic->Api->StreamSend(
        hStream,
        Buffers.data(),
        Buffers.size(),
        Flags,
        Context
    ));
}

void MsQuicStream::SetStreamHandler(IMsQuicStreamHandler* ObserverIn) 
{
    Observer = ObserverIn;
}
