/**
 * @file main.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <chrono>
#include <ftl/protocol.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/time.hpp>
#include <ftl/protocol/channels.hpp>
#include <ftl/protocol/codecs.hpp>
#include <ftl/lib/loguru.hpp>

using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using ftl::protocol::Channel;
using ftl::protocol::Codec;

int main(int argc, char *argv[]) {
    if (argc != 2) return -1;

    ftl::getSelf()->listen("tcp://localhost:9000");

    auto stream = ftl::createStream(argv[1]);

    auto h = stream->onError([](ftl::protocol::Error, const std::string &str) {
        LOG(ERROR) << str;
        return true;
    });

    if (!stream->begin()) return -1;

    int count = 10;
    while (count--) {
        StreamPacket spkt;
        DataPacket pkt;

        spkt.timestamp = ftl::time::get_time();
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = Channel::kColour;
        pkt.codec = Codec::kJPG;

        stream->post(spkt, pkt);
        sleep_for(milliseconds(100));
    }

    stream->end();

    return 0;
}
