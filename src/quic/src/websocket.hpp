#pragma once

#include <ftl/utility/base64.hpp>
#include <loguru.hpp>
#include <ftl/lib/span.hpp>

#include <deque>
#include <cstring>
#include <cstdlib>

#include <array>

enum OpCodeType
{
    CONTINUATION = 0x0,
    TEXT_FRAME = 0x1,
    BINARY_FRAME = 0x2,
    CLOSE = 8,
    PING = 9,
    PONG = 0xA,
};

using WsMaskKey = std::array<uint8_t, 4>;

struct WebSocketHeader
{
    bool     Fin;
    uint8_t  Rsv;
    bool     Mask;
    OpCodeType OpCode;
    uint64_t PayloadLength;
    WsMaskKey MaskingKey;
    uint8_t HeaderSize;
};

inline WsMaskKey GenerateMaskingKey()
{
    WsMaskKey Key;
    Key[0] = rand() & 255;
    Key[1] = rand() & 255;
    Key[2] = rand() & 255;
    Key[3] = rand() & 255;
    return Key;
}

/// XOR the buffer with given key. Offset (bytes masked so far) may be used to continue partially masked buffer.
template<typename T>
inline uint32_t Mask(T* Buffer, int BufferSize, WsMaskKey& Key, uint32_t Offset = 0)
{
    for (int32_t i = 0; i < BufferSize; i++)
    {
        Buffer[i] ^= Key[(i + Offset) & 3];
    }
    return BufferSize;
}

inline void WsWriteMaskingKey(uint8_t* Buffer, WsMaskKey Key)
{
    Buffer[0] = Key[0];
    Buffer[1] = Key[1];
    Buffer[2] = Key[2];
    Buffer[3] = Key[3];
}

inline int WsWriteHeader(OpCodeType Op, bool UseMask, WsMaskKey Mask, const size_t Length, uint8_t* Buffer, size_t MaxLength)
{
    CHECK(MaxLength >= 12);

    uint8_t *Header = Buffer;
    size_t HeaderSize = 2 + (Length >= 126 ? 2 : 0) + (Length >= 65536 ? 6 : 0) + (UseMask ? 4 : 0);
    if (HeaderSize > MaxLength) return -1;

    Header[0] = 0x80 | int8_t(Op);
    if (Length < 126)
    {
        Header[1] = (Length & 0xFF) | (UseMask ? 0x80 : 0);
        if (UseMask) { WsWriteMaskingKey(Header + 2, Mask); }
    } 
    else if (Length < 65536)
    {
        Header[1] = 126 | (UseMask ? 0x80 : 0);
        Header[2] = (Length >> 8) & 0xFF;
        Header[3] = (Length >> 0) & 0xFF;
        if (UseMask) { WsWriteMaskingKey(Header + 4, Mask); }
    }
    else
    {
        Header[1] = 127 | (UseMask ? 0x80 : 0);
        Header[2] = (Length >> 56) & 0xFF;
        Header[3] = (Length >> 48) & 0xFF;
        Header[4] = (Length >> 40) & 0xFF;
        Header[5] = (Length >> 32) & 0xFF;
        Header[6] = (Length >> 24) & 0xFF;
        Header[7] = (Length >> 16) & 0xFF;
        Header[8] = (Length >>  8) & 0xFF;
        Header[9] = (Length >>  0) & 0xFF;

        if (UseMask) { WsWriteMaskingKey(Header + 10, Mask); }
    }
    
    /*static std::atomic_int ws_count = 0;
    LOG(INFO) << "[Quic/WebSocket] Send (" << ws_count++ << "): "
        << "OpCode: " << BINARY_FRAME << ", size (header): " <<  HeaderSize << ", size (payload): "
        << Length;*/

    return static_cast<int>(HeaderSize);
}


enum WsParseHeaderStatus
{
    OK,
    NOT_ENOUGH_DATA,
    INVALID
};

inline WsParseHeaderStatus WsParseHeader(uint8_t* const Data, size_t Length, WebSocketHeader& Header)
{
    if (Length < 2) { return NOT_ENOUGH_DATA; }
    Header.Fin = (Data[0] & 0x80) == 0x80;
    Header.Rsv = (Data[0] & 0x70);
    Header.OpCode = (OpCodeType) (Data[0] & 0x0F);
    Header.Mask = (Data[1] & 0x80) == 0x80;
    auto PayloadLength = (Data[1] & 0x7F);
    Header.HeaderSize = 2 + (PayloadLength == 126 ? 2 : 0) + (PayloadLength == 127? 8 : 0) + (Header.Mask? 4 : 0);
    CHECK(Header.HeaderSize <= 14);

    if (Length < Header.HeaderSize) { return NOT_ENOUGH_DATA; } // header incomplete
    if (Header.Rsv != 0) { return INVALID; } // must be 0 according to RFC

    // invalid opcode, corrupted header?
    if ((Header.OpCode > 10) || ((Header.OpCode > 2) && (Header.OpCode < 8))) { return INVALID; } // invalid OpCode

    int i = 0;
    if (PayloadLength < 126)
    {
        Header.PayloadLength = PayloadLength;
        i = 2;
    }
    else if (PayloadLength == 126)
    {
        Header.PayloadLength = 0;
        Header.PayloadLength |= ((uint64_t) Data[2]) << 8;
        Header.PayloadLength |= ((uint64_t) Data[3]) << 0;
        i = 4;
    }
    else if (PayloadLength == 127)
    {
        Header.PayloadLength = 0;
        Header.PayloadLength |= ((uint64_t) Data[2]) << 56;
        Header.PayloadLength |= ((uint64_t) Data[3]) << 48;
        Header.PayloadLength |= ((uint64_t) Data[4]) << 40;
        Header.PayloadLength |= ((uint64_t) Data[5]) << 32;
        Header.PayloadLength |= ((uint64_t) Data[6]) << 24;
        Header.PayloadLength |= ((uint64_t) Data[7]) << 16;
        Header.PayloadLength |= ((uint64_t) Data[8]) << 8;
        Header.PayloadLength |= ((uint64_t) Data[9]) << 0;
        i = 10;
    }

    if (Header.Mask)
    {
        // Byte order?
        Header.MaskingKey[0] = uint8_t(Data[i + 0]);
        Header.MaskingKey[1] = uint8_t(Data[i + 1]);
        Header.MaskingKey[2] = uint8_t(Data[i + 2]);
        Header.MaskingKey[3] = uint8_t(Data[i + 3]);
    }
    else
    {
        Header.MaskingKey = WsMaskKey{0, 0, 0, 0};
    }

    /*static std::atomic_int ws_count = 0;
    LOG(INFO) << "[WebSocket] Recv (" << ws_count++ << "): "
        << "OpCode: " << Header.OpCode << ", size (header): " <<  (int)Header.HeaderSize << ", size (payload): "
        << Header.PayloadLength;*/
    
    return OK;
}

struct WebsocketRead
{
private:
    int ws_payload_read_ = 0; // unmasking partially read buffers
    int ws_payload_remaining_ = 0; // remaining data in current buffer
    int ws_partial_header_read_ = 0;  // number of bytes in partially received header
    bool ws_mask_ = false; // use mask?
    nonstd::span<uint8_t> buffer_; // current input buffer
    bool error_ = false; // decoding failed?

    std::array<unsigned char, 4> ws_mask_key_; // masking key
    std::array<uint8_t, 14> ws_header_; // internal buffer for partial header

    inline void CopyPartialHeader()
    {
        CHECK(ws_partial_header_read_ < int(ws_header_.size()));
        size_t cpy_size = std::min<size_t>(ws_header_.size() - ws_partial_header_read_, buffer_.size());
        memcpy(ws_header_.data() + ws_partial_header_read_, buffer_.data(), cpy_size);
        ws_partial_header_read_ += cpy_size;
        CHECK(ws_partial_header_read_ <= int(ws_header_.size()));
        buffer_ = { buffer_.data() + cpy_size, buffer_.size() - cpy_size };
    }

    inline nonstd::span<uint8_t> NextBuffer()
    {
        auto read_size = std::min<int32_t>(buffer_.size(), ws_payload_remaining_);
        auto buffer = nonstd::span<uint8_t>(buffer_.data(), read_size);

        buffer_ = { buffer_.data() + read_size, buffer_.size() - read_size };

        if (ws_mask_) { Mask(buffer.data(), buffer.size(), ws_mask_key_, ws_payload_read_); }
        ws_payload_read_ += read_size;
        ws_payload_remaining_ -= read_size;
        
        return buffer;
    }

public:
    inline WebsocketRead()
    {

    }

    // Discard any state
    inline void Reset()
    {
        ws_payload_read_ = 0;
        ws_payload_remaining_ = 0;
        ws_partial_header_read_ = 0;
        ws_mask_ = false;
        buffer_ = {};
        error_ = false;
    }

    // Process buffer, call Buffer() until it returns empty value before calling. 
    // Once Buffer() returns empty span, all input is processed.
    inline void Next(nonstd::span<uint8_t> data)
    {
        CHECK(buffer_.size() == 0) << "New buffer passed before preivous buffer was completely processed";
        buffer_ = data;
    }

    inline bool Error() const { return error_; }

    // Get next decoded buffer, if any
    inline nonstd::span<uint8_t> Buffer()
    {
        if (buffer_.size() == 0) return {}; // No data

        if (ws_payload_remaining_ > 0)
        {
            return NextBuffer();
        }

        WebSocketHeader header;
        WsParseHeaderStatus status = INVALID;

        if (ws_partial_header_read_ > 0)
        {
            // Got partial bytes of header, copy remaining and try again
            CopyPartialHeader();
            status = WsParseHeader(ws_header_.data(), ws_partial_header_read_, header);
        }
        else
        {
            status = WsParseHeader(buffer_.data(), buffer_.size(), header);
        }

        if (status == INVALID)
        {
            LOG(ERROR) << "[WebSocket] Invalid header";
            error_ = true;
            return {};
        }
        else if (status == NOT_ENOUGH_DATA)
        {
            CHECK((buffer_.size() + ws_partial_header_read_) < ws_header_.size());
            if (ws_partial_header_read_ == 0) { CopyPartialHeader(); }
            else                              { /* already copied */ }
            return {};
        }

        CHECK(status == OK);
        
        if (ws_partial_header_read_ == 0)
        {
            // Not partial header, just skip the bytes header
            buffer_ = { buffer_.data() + header.HeaderSize, buffer_.size() - header.HeaderSize };
        }
        else
        {
            // Partial header read might have gone past the header bytes, adjust if necessary
            CHECK(ws_partial_header_read_ <= header.HeaderSize);
            auto adjust = ws_partial_header_read_ - header.HeaderSize;
            buffer_ = { buffer_.data() - adjust, buffer_.size() + adjust };
        }

        if (header.OpCode == OpCodeType::CLOSE)
        {
            LOG(WARNING) << "[WebSocket] Received close control frame";
            error_ = true;
            return {};
        }
        
        ws_mask_ = header.Mask;
        ws_mask_key_ = header.MaskingKey;

        ws_partial_header_read_ = 0;
        ws_payload_read_ = 0;
        ws_payload_remaining_ = header.PayloadLength;

        if (header.OpCode != OpCodeType::BINARY_FRAME)
        {
            LOG(WARNING) << "[WebSocket] Received non-binary frame "
                            << "(OpCode: " << header.OpCode << ", size (header): " <<  header.HeaderSize << ", size (payload): "
                            << header.PayloadLength << ").";

            if (header.OpCode == OpCodeType::TEXT_FRAME)
            {
                LOG(WARNING) << "[Quic/WebSocket] Text Frame: " 
                             << std::string_view((char*)buffer_.data(), std::min(header.PayloadLength, buffer_.size()));
            }
        }

        return NextBuffer();
    }
};
