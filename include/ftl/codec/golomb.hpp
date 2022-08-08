#pragma once

#include <cstddef>
#include <cstdint>

namespace ftl {
namespace codec {
namespace detail {

extern const uint8_t golomb_len[512];
extern const uint8_t golomb_ue_code[512];
extern const int8_t golomb_se_code[512];

struct ParseContext {
    const uint8_t *ptr;
    size_t index;
    size_t length;
};

static inline uint32_t bswap_32(uint32_t x) {
    x= ((x<<8)&0xFF00FF00) | ((x>>8)&0x00FF00FF);
    x= (x>>16) | (x<<16);
    return x;
}

static inline uint32_t read32(const uint8_t *ptr) {
    return bswap_32(*reinterpret_cast<const uint32_t*>(ptr));
}

static inline unsigned int getBits(ParseContext *ctx, int cnt) {
    uint32_t buf = read32(&ctx->ptr[ctx->index >> 3]) << (ctx->index & 0x07);
    ctx->index += cnt;
    return buf >> (32 - cnt);
}

static inline unsigned int getBits1(ParseContext *ctx) {
    return getBits(ctx, 1);
}

static inline int log2(unsigned int x) {
    #ifdef __GNUC__
    return (31 - __builtin_clz((x)|1));
    #elif _MSC_VER
    unsigned long n;
    _BitScanReverse(&n, x|1);
    return n;
    #else
    return 0;  // TODO(Nick)
    #endif
}

static inline unsigned int golombUnsigned31(ParseContext *ctx) {
    uint32_t buf = read32(&ctx->ptr[ctx->index >> 3]) << (ctx->index & 0x07);
    buf >>= 32 - 9;
    ctx->index += golomb_len[buf];
    return golomb_ue_code[buf];
}

static inline unsigned int golombUnsigned(ParseContext *ctx) {
    uint32_t buf = read32(&ctx->ptr[ctx->index >> 3]) << (ctx->index & 0x07);

    if (buf >= (1<<27)) {
        buf >>= 32 - 9;
        ctx->index += golomb_len[buf];
        return golomb_ue_code[buf];
    } else {
        int log = 2 * log2(buf) - 31;
        buf >>= log;
        buf--;
        ctx->index += 32 - log;
        return buf;
    }
}

static inline int golombSigned(ParseContext *ctx) {
    uint32_t buf = read32(&ctx->ptr[ctx->index >> 3]) << (ctx->index & 0x07);

    if (buf >= (1<<27)) {
        buf >>= 32 - 9;
        ctx->index += golomb_len[buf];
        return golomb_se_code[buf];
    } else {
        int log = 2 * log2(buf) - 31;
        buf >>= log;
        ctx->index += 32 - log;

        if(buf & 1) return -static_cast<int>(buf>>1);
        else return buf >> 1;
    }
}

}
}
}