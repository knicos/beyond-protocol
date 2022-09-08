/**
 * @file main.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <chrono>
#include <ftl/protocol.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/node.hpp>
#include <ftl/time.hpp>
#include <ftl/protocol/channels.hpp>
#include <ftl/protocol/codecs.hpp>
#include <ftl/lib/loguru.hpp>
#include <ftl/protocol/muxer.hpp>
#include <nlohmann/json.hpp>

using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using ftl::protocol::Channel;
using ftl::protocol::Codec;

int main(int argc, char *argv[]) {
    if (argc != 3) return -1;

    ftl::getSelf()->onNodeDetails([]() {
        return nlohmann::json{
            {"id", ftl::protocol::id.to_string()},
            {"title", "Test app"},
            { "gpus", nlohmann::json::array() },
            { "devices", nlohmann::json::array() }
        };
    });

    ftl::getSelf()->listen("tcp://localhost:9000");
    auto node = ftl::connectNode(argv[1]);
    node->waitConnection();

    auto muxer = std::make_unique<ftl::protocol::Muxer>();
    muxer->begin();

    auto stream = ftl::createStream(argv[2]);
    muxer->add(stream);

    auto h = muxer->onError([](ftl::protocol::Error, const std::string &str) {
        LOG(ERROR) << str;
        return true;
    });

    auto rh = muxer->onRequest([](const ftl::protocol::Request &r) {
        LOG(INFO) << "Got request " << r.id.frameset() << "," << r.id.source();
        return true;
    });

    stream->seen(ftl::protocol::FrameID(0, 0), Channel::kEndFrame);

    if (!stream->begin()) return -1;

    sleep_for(milliseconds(100));

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

    LOG(INFO) << "Done";

    muxer->end();

    return 0;
}
