#include <ftl/utility/base64.hpp>

#include <deque>
#include <cstring>
#include <cstdlib>

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

WsMaskKey GenerateMaskingKey()
{
    WsMaskKey Key;
    Key[0] = rand() & 255;
    Key[1] = rand() & 255;
    Key[2] = rand() & 255;
    Key[3] = rand() & 255;
    return Key;
}

/// XOR the buffer with given key. Offset (bytes masked so far) may be used to continue partially masked buffer.
uint32_t Mask(char* Buffer, int BufferSize, WsMaskKey& Key, uint32_t Offset = 0)
{
    for (int32_t i = 0; i < BufferSize; i++)
    {
        Buffer[i] ^= Key[(i + Offset) & 3];
    }
    return BufferSize;
}

void WsWriteMaskingKey(char* Buffer, WsMaskKey Key)
{
    Buffer[0] = Key[0];
    Buffer[1] = Key[1];
    Buffer[2] = Key[2];
    Buffer[3] = Key[3];
}

int WsWriteHeader(OpCodeType Op, bool UseMask, WsMaskKey Mask, const size_t Length, char* Buffer, size_t MaxLength)
{
    CHECK(MaxLength >= 12);

    char *Header = Buffer;
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

WsParseHeaderStatus WsParseHeader(char* const DataIn, size_t Length, WebSocketHeader& Header)
{
    uint8_t* const Data = (uint8_t* const) DataIn; // Type must be unsigned (bit shifts)

    if (Length < 2) { return NOT_ENOUGH_DATA; }
    Header.Fin = (Data[0] & 0x80) == 0x80;
    Header.Rsv = (Data[0] & 0x70);
    Header.OpCode = (OpCodeType) (Data[0] & 0x0F);
    Header.Mask = (Data[1] & 0x80) == 0x80;
    auto PayloadLength = (Data[1] & 0x7F);
    Header.HeaderSize = 2 + (PayloadLength == 126 ? 2 : 0) + (PayloadLength == 127? 8 : 0) + (Header.Mask? 4 : 0);

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
