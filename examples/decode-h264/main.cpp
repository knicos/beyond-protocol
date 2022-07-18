/**
 * @file main.cpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#include <chrono>
#include <ftl/protocol.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/lib/loguru.hpp>
#include <ftl/codec/h264.hpp>

using ftl::protocol::Codec;
using ftl::protocol::Channel;
using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using std::this_thread::sleep_for;
using std::chrono::seconds;
using ftl::protocol::StreamProperty;

int main(int argc, char *argv[]) {
    if (argc != 2) return -1;

    auto stream = ftl::getStream(argv[1]);

    const auto parser = std::make_unique<ftl::codec::h264::Parser>();

    auto h = stream->onPacket([&parser](const StreamPacket &spkt, const DataPacket &pkt) {
        if (spkt.channel == Channel::kColour && pkt.codec == Codec::kH264) {
            try {
                auto slices = parser->parse(pkt.data);
                for (const ftl::codec::h264::Slice &s : slices) {
                    LOG(INFO) << "Slice (" << spkt.timestamp << ")" << std::endl << ftl::codec::h264::prettySlice(s);
                }
            } catch (const std::exception &e) {
                LOG(ERROR) << e.what();
            }
        }
        return true;
    });

    stream->setProperty(StreamProperty::kLooping, true);
    stream->setProperty(StreamProperty::kSpeed, 1);

    if (!stream->begin()) return -1;
    sleep_for(seconds(20));
    stream->end();

    return 0;
}
