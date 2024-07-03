#include "../catch.hpp"

#include <websocket.hpp>

#include <random>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <vector>
#include <numeric>

template<typename T1, typename T2>
inline bool buffers_equal(const T1& buf1, const T2& buf2)
{
    if (buf1.size() != buf2.size()) { return false; }
    for (size_t i = 0; i < buf1.size(); i++)
    {
        if (buf1[i] != buf2[i]) { return false; }
    }
    return true;
}

static std::random_device rnd_device;
static std::mt19937 mersenne_engine { rnd_device() };

template<typename T>
inline void generate_random_buffer(T& buffer)
{
    std::uniform_int_distribution<int> dist {0, 255};
    auto gen = [&](){ return dist(mersenne_engine); };
    std::generate(buffer.begin(), buffer.end(), gen);
}

TEST_CASE("Websocket framing")
{    
    SECTION("Write and read a single complete websocket frame")
    {
         // header size depends on payload length and whether or not masking is used
        auto payload_size = GENERATE(1, 10, 15, 16, 126, 127, 1500);
        auto use_mask = GENERATE(false, true);

        WsMaskKey mask_key;
        generate_random_buffer(mask_key);
        std::vector<uint8_t> buffer_in(payload_size);
        generate_random_buffer(buffer_in);

        std::vector<uint8_t> buffer_out(1024);

        auto offset = WsWriteHeader(
            OpCodeType::BINARY_FRAME, use_mask, mask_key,
            buffer_in.size(), buffer_out.data(), buffer_out.size());
        
        REQUIRE(offset > 0);
        buffer_out.resize(buffer_in.size() + offset);

        memcpy(buffer_out.data() + offset, buffer_in.data(), buffer_in.size());
        if (use_mask) { Mask(buffer_out.data() + offset, buffer_out.size() - offset, mask_key, 0); }

        std::vector<uint8_t> buffer_decoded;
        WebsocketRead reader;
        reader.Next(buffer_out);
        auto buffer = reader.Buffer();
        REQUIRE(!reader.Error());
        buffer_decoded.insert(buffer_decoded.end(), buffer.begin(), buffer.end());
        REQUIRE(buffers_equal(buffer_in, buffer_decoded));
    }

    SECTION("Process partial frames")
    {
        // Could also randomize payload sizes
        std::vector<int> sizes = {
            18, 12,  1,  2, 20, 63, 140,
             5,  6,  7,  8,  9, 11, 103, 
            15, 17, 19, 47, 42,  3, 1 };
        std::shuffle(sizes.begin(), sizes.end(), mersenne_engine);

        std::vector<uint8_t> payloads(std::reduce(sizes.begin(), sizes.end()));

        auto use_mask = GENERATE(false, true);
        generate_random_buffer(payloads);

        std::vector<uint8_t> buffer_out;
        std::vector<size_t> offsets; // index for beginning of each frame
        
        // Generate websocket frames of various sizes and write them to payloads vector
        {
            buffer_out.reserve(1024);
            size_t offset_in = 0;   // input buffer offset
            size_t offset = 0;      // output buffer offset

            for (const auto sz : sizes)
            {
                WsMaskKey mask_key;
                generate_random_buffer(mask_key);

                buffer_out.resize(offset + 14); // reserve enough space for the header
                auto header_size = WsWriteHeader(
                    OpCodeType::BINARY_FRAME, use_mask, mask_key,
                    sz, buffer_out.data() + offset, buffer_out.size());
            
                REQUIRE(header_size > 0);

                offsets.push_back(offset); // beginning of current frame
                offset += header_size;

                buffer_out.resize(offset + sz); // reserve enough space for payload
                memcpy(buffer_out.data() + offset, payloads.data() + offset_in, sz);
                if (use_mask) { Mask(buffer_out.data() + offset, sz, mask_key, 0); }
                offset_in += sz;
                offset += sz;
            }
            REQUIRE(offset_in == payloads.size());
            REQUIRE(offset == buffer_out.size());
        }

        SECTION("Read websocket frames in chunks of different sizes")
        {
            // TODO: For completeness, should chunk the input buffer to explicitly cover the cases
            //       of partial buffers, where passed buffer chunk contains:
            //        * beginning of header
            //        * end of header
            //        * end of payload and partial header
            //        * end of header and beginning of header
            //        ...
            //
            // Different sized chunks here should catch most of the obvious cases

            auto read_sz = GENERATE(1, 2, 3, 5, 7, 10, 13, 17, 60, 64, 100, 1000);
            std::vector<uint8_t> payloads_decoded;
            WebsocketRead reader;

            auto* end = buffer_out.data() + buffer_out.size();
            size_t n_chunks = (buffer_out.size() + read_sz)/read_sz;
            for (size_t i = 1; i <= n_chunks; i++)
            {
                auto* ptr  = buffer_out.data();
                auto* prev = ptr + (i - 1)*read_sz;
                auto* cur  = ptr + i*read_sz;
                auto partial_buffer = nonstd::span<uint8_t>(prev, std::min(cur, end));

                reader.Next(partial_buffer);
                nonstd::span<uint8_t> buffer;
                do
                {
                    payloads_decoded.insert(payloads_decoded.end(), buffer.begin(), buffer.end());
                    buffer = reader.Buffer();
                    REQUIRE(!reader.Error());
                }
                while(buffer.size() > 0);
            }

            REQUIRE(buffers_equal(payloads, payloads_decoded));
        }
    }
}
