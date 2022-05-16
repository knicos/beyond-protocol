/**
 * @file main.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <chrono>
#include <ftl/protocol.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/lib/loguru.hpp>

using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using std::this_thread::sleep_for;
using std::chrono::seconds;
using ftl::protocol::StreamProperty;

int main(int argc, char *argv[]) {
    if (argc != 2) return -1;

    auto stream = ftl::getStream(argv[1]);

    auto h = stream->onPacket([](const StreamPacket &spkt, const DataPacket &pkt) {
        LOG(INFO) << "Packet: "
            << static_cast<int>(spkt.streamID) << ","
            << static_cast<int>(spkt.frame_number) << ","
            << static_cast<int>(spkt.channel);
        return true;
    });

    stream->setProperty(StreamProperty::kLooping, true);
    stream->setProperty(StreamProperty::kSpeed, 1);

    if (!stream->begin()) return -1;
    sleep_for(seconds(5));
    stream->end();

    return 0;
}
