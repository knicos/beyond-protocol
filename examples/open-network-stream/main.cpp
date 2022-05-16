/**
 * @file main.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <chrono>
#include <ftl/protocol.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/protocol/node.hpp>
#include <ftl/lib/loguru.hpp>

using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using std::this_thread::sleep_for;
using std::chrono::seconds;

int main(int argc, char *argv[]) {
    if (argc != 3) return -1;

    auto node = ftl::connectNode(argv[1]);
    node->waitConnection();

    // TODO(Nick): Query available streams

    auto stream = ftl::getStream(argv[2]);

    auto h = stream->onPacket([](const StreamPacket &spkt, const DataPacket &pkt) {
        LOG(INFO) << "Packet: "
            << static_cast<int>(spkt.streamID) << ","
            << static_cast<int>(spkt.frame_number) << ","
            << static_cast<int>(spkt.channel);
        return true;
    });

    if (!stream->begin()) return -1;
    sleep_for(seconds(5));
    stream->end();

    return 0;
}
