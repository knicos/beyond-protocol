#include "catch.hpp"

#include <filesystem>
#include <ftl/protocol/streams.hpp>
#include <ftl/protocol.hpp>
#include <ftl/time.hpp>

using ftl::protocol::Channel;
using ftl::protocol::Codec;
using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;

static int ctr = 0;

TEST_CASE("File write and read", "[stream]") {
    std::string file = "ftl_file_stream_test" + std::to_string(ctr++) + ".ftl";
    std::string filename = (std::filesystem::temp_directory_path() / file).string();

    SECTION("write read single packet") {
        auto writer = ftl::createStream(filename);

        REQUIRE( writer->begin() );

        REQUIRE( writer->post({4,ftl::time::get_time(),2,1, Channel::kConfidence},{Codec::kAny, 0, 0, 0, 0, {'f'}}) );

        writer->end();

        auto reader = ftl::getStream(filename);

        StreamPacket tspkt = {4,0,0,1, Channel::kColour};
        auto h = reader->onPacket([&tspkt](const StreamPacket &spkt, const DataPacket &pkt) {
            if (spkt.channel == Channel::kEndFrame) return true;
            tspkt = spkt;
            return true;
        });
        REQUIRE( reader->begin() );

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        reader->end();

        //REQUIRE( tspkt.timestamp == 0 );
        REQUIRE( tspkt.streamID == (uint8_t)2 );
        REQUIRE( tspkt.channel == Channel::kConfidence );
    }

    SECTION("write read multiple packets at same timestamp") {
        auto writer = ftl::createStream(filename);

        REQUIRE( writer->begin() );

        REQUIRE( writer->post({5,10,0,1, Channel::kConfidence},{Codec::kAny, 0, 0, 0, 0, {'f'}}) );
        REQUIRE( writer->post({5,10,1,1, Channel::kDepth},{Codec::kAny, 0, 0, 0, 0, {'f'}}) );
        REQUIRE( writer->post({5,10,2,1, Channel::kScreen},{Codec::kAny, 0, 0, 0, 0, {'f'}}) );

        writer->end();

        auto reader = ftl::getStream(filename);

        StreamPacket tspkt = {5,0,0,1, Channel::kColour};
        std::atomic_int count = 0;
        
        auto h = reader->onPacket([&tspkt,&count](const StreamPacket &spkt, const DataPacket &pkt) {
            if (spkt.channel == Channel::kEndFrame) return true;
            tspkt = spkt;
            ++count;
            return true;
        });
        REQUIRE( reader->begin() );

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        reader->end();

        REQUIRE( count == 3 );
        REQUIRE( tspkt.timestamp > 0 );
        REQUIRE( tspkt.streamID == 2 );
        REQUIRE( tspkt.channel == Channel::kScreen );
    }

    SECTION("write read multiple packets at different timestamps") {
        auto writer = ftl::createStream(filename);

        REQUIRE( writer->begin() );

        auto time = ftl::time::get_time();
        REQUIRE( writer->post({4,time,0,0, Channel::kConfidence},{Codec::kAny, 0, 1, 0, 0, {'f'}}) );
        REQUIRE( writer->post({4,time+50,0,0,Channel::kDepth},{Codec::kAny, 0, 1, 0, 0, {'f'}}) );
        REQUIRE( writer->post({4,time+2*50,0,0,Channel::kScreen},{Codec::kAny, 0, 1, 0, 0, {'f'}}) );

        writer->end();

        auto reader = ftl::getStream(filename);

        StreamPacket tspkt = {0,0,0,1,Channel::kColour};
        int count = 0;
        int avgDiff = -1;
        std::vector<Channel> channels;
        channels.reserve(3);

        auto h = reader->onPacket([&tspkt,&count,&avgDiff, &channels](const StreamPacket &spkt, const DataPacket &pkt) {
            if (spkt.channel == Channel::kEndFrame) return true;

            if (tspkt.timestamp > 0) {
                avgDiff += spkt.timestamp - tspkt.timestamp;
            }
            tspkt = spkt;
            ++count;
            channels.push_back(spkt.channel);
            return true;
        });
        REQUIRE( reader->begin() );

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        avgDiff = avgDiff / (count - 1);
        REQUIRE( count == 3 );
        REQUIRE( avgDiff <= 50 );
        REQUIRE( avgDiff >= 48 );
        REQUIRE( channels[0] == Channel::kConfidence );
        REQUIRE( channels[1] == Channel::kDepth );
        REQUIRE( channels[2] == Channel::kScreen );
    }
}
