#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/uri.hpp>
#include <ftl/exception.hpp>
#include <ftl/protocol/node.hpp>

using ftl::protocol::FrameID;
using ftl::protocol::StreamProperty;

// --- Mock --------------------------------------------------------------------



// --- Tests -------------------------------------------------------------------

TEST_CASE("TCP Stream", "[net]") {
    std::mutex mtx;

    auto self = ftl::createDummySelf();
    self->listen(ftl::URI("tcp://localhost:0")); 

    auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
    LOG(INFO) << uri;
    auto p = ftl::connectNode(uri);
    p->waitConnection(5);

    SECTION("bulk concurrent packets") {
        MUTEX mtx;
        std::vector<int64_t> times;
        times.reserve(2000);

        auto s1 = ftl::createStream("ftl://mystream");
        REQUIRE( s1 );

        auto s2 = self->getStream("ftl://mystream");
        REQUIRE( s2 );

        auto h = s2->onPacket([&mtx, &times](const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
            UNIQUE_LOCK(mtx, lk);
            times.push_back(spkt.timestamp);
            return true;
        });

        s1->begin();
        s2->begin();

        REQUIRE(s1->active(FrameID(0, 0)) == false);

        s2->enable(FrameID(0, 0), {ftl::protocol::Channel::kColour});

        // TODO: Find better option
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        REQUIRE(s1->active(FrameID(0, 0)) == true);

        ftl::pool.push([s1](int p) {
            ftl::protocol::StreamPacket spkt;
            spkt.streamID = 0;
            spkt.frame_number = 0;
            spkt.channel = ftl::protocol::Channel::kEndFrame;
            ftl::protocol::DataPacket pkt;
            pkt.bitrate = 10;
            pkt.codec = ftl::protocol::Codec::kJPG;
            pkt.packet_count = 2;
            for (int i=0; i<1000; ++i) {
                spkt.timestamp = 100 + i;
                s1->post(spkt, pkt);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        ftl::pool.push([s1](int p) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ftl::protocol::StreamPacket spkt;
            spkt.streamID = 0;
            spkt.frame_number = 0;
            spkt.channel = ftl::protocol::Channel::kColour;
            ftl::protocol::DataPacket pkt;
            for (int i=0; i<1000; ++i) {
                spkt.timestamp = 100 + i;
                s1->post(spkt, pkt);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        // TODO: Find better option
        int k = 100;
        while (--k > 0 && times.size() < 2000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        REQUIRE( times.size() == 2000 );

        int64_t minDiff = 0;

        for (size_t i = 1; i < times.size(); ++i) {
            const int64_t diff = times[i] - times[i - 1];
            if (diff < minDiff) minDiff = diff;
        }

        REQUIRE(minDiff == 0);
    }

    p.reset();
    ftl::protocol::reset();
}
