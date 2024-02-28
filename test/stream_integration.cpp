#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/uri.hpp>
#include <ftl/exception.hpp>
#include <ftl/protocol/node.hpp>
#include <ftl/time.hpp>

#include <future>

using ftl::protocol::FrameID;
using ftl::protocol::StreamProperty;

static auto TEST_TIMEOUT = std::chrono::milliseconds(1500);

// --- Tests -------------------------------------------------------------------

TEST_CASE("TCP Stream", "[net]") {
    std::mutex mtx;

    auto self = ftl::createDummySelf();
    self->listen(ftl::URI("tcp://localhost:0")); 

    auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
    LOG(INFO) << uri;
    auto p = ftl::connectNode(uri);
    p->waitConnection(5);

    SECTION("fails if stream doesn't exist") {
        auto s1 = self->getStream("ftl://mystream_bad");
        REQUIRE( s1 );

        auto seenError = ftl::protocol::Error::kNoError;
        auto h = s1->onError([&seenError](ftl::protocol::Error err, const std::string &str) {
            seenError = err;
            return true;
        });

        REQUIRE( s1->begin() );
        REQUIRE( !s1->enable(FrameID(0, 0)) );
        REQUIRE( seenError == ftl::protocol::Error::kURIDoesNotExist );
    }

    SECTION("single enabled packet stream") {
        std::condition_variable cv;
        std::unique_lock<std::mutex> lk(mtx);

        auto s1 = ftl::createStream("ftl://mystream");
        REQUIRE( s1 );

        auto s2 = self->getStream("ftl://mystream");
        REQUIRE( s2 );

        ftl::protocol::DataPacket rpkt;
        rpkt.bitrate = 20;

        auto h = s2->onPacket([&cv, &rpkt](const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
            rpkt = pkt;
            cv.notify_one();
            return true;
        });

        std::promise<bool> seenReqPromise;
        auto seenReqFuture = seenReqPromise.get_future();
        auto h2 = s1->onRequest([&](const ftl::protocol::Request &req) {
            seenReqPromise.set_value(true);
            return true;
        });

        s1->begin();
        s2->begin();

        s2->enable(FrameID(0, 0));

        seenReqFuture.wait_for(TEST_TIMEOUT);
        REQUIRE((seenReqFuture.valid() && seenReqFuture.get()));

        ftl::protocol::StreamPacket spkt;
        spkt.timestamp = 0;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = ftl::protocol::Channel::kColour;
        ftl::protocol::DataPacket pkt;
        pkt.bitrate = 10;
        pkt.codec = ftl::protocol::Codec::kJPG;
        pkt.frame_count = 1;
        s1->post(spkt, pkt);

        bool r = cv.wait_for(lk, std::chrono::seconds(5), [&rpkt](){ return rpkt.bitrate == 10; });
        REQUIRE( r );
        REQUIRE( rpkt.bitrate == 10 );
        REQUIRE( rpkt.codec == ftl::protocol::Codec::kJPG );
        REQUIRE( rpkt.frame_count == 1 );

        REQUIRE( std::any_cast<size_t>(s1->getProperty(StreamProperty::kObservers)) == 1 );
    }

    SECTION("stops sending when request expires") {
        std::atomic_int rcount = 0;
        auto s1 = ftl::createStream("ftl://mystream");
        REQUIRE( s1 );

        auto s2 = self->getStream("ftl://mystream");
        REQUIRE( s2 );

        auto h = s2->onPacket([&rcount](const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
            ++rcount;
            return true;
        });

        s2->setProperty(ftl::protocol::StreamProperty::kRequestSize, 30);

        s1->begin();
        s2->begin();

        REQUIRE(s1->active(FrameID(0, 0)) == false);

        s2->enable(FrameID(0, 0));

        // FIXME
        std::this_thread::sleep_for(TEST_TIMEOUT);

        REQUIRE(s1->active(FrameID(0, 0)) == true);

        ftl::protocol::StreamPacket spkt;
        spkt.timestamp = 0;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = ftl::protocol::Channel::kEndFrame;
        ftl::protocol::DataPacket pkt;
        pkt.bitrate = 10;
        pkt.codec = ftl::protocol::Codec::kJPG;
        pkt.packet_count = 1;

        for (int i=0; i<30 + 20; ++i) {
            spkt.timestamp = i;
            s1->post(spkt, pkt);
        }

        // FIXME
        int k = 20;
        while (--k > 0 && rcount < 30) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        REQUIRE( rcount == 30 );
    }

    SECTION("receives at correct rate") {
        std::atomic_int rcount = 0;
        std::atomic_int totalDelay = 0;
        int64_t lastTs = 0;
        auto s1 = ftl::createStream("ftl://mystream");
        REQUIRE(s1);

        auto s2 = self->getStream("ftl://mystream");
        REQUIRE(s2);

        // Large enough buffering value so the queue can't grow to large (otherwise fast forwards until buffer at desired length)
        s2->setProperty(ftl::protocol::StreamProperty::kBuffering, 0.05f);

        auto h = s2->onPacket([&rcount, &lastTs, &totalDelay](const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
            if (lastTs == 0) {
                lastTs = ftl::time::get_time();
            } else {
                int64_t now = ftl::time::get_time();
                int64_t delta = now - lastTs;
                lastTs = now;
                totalDelay += delta;
                ++rcount;
            }
            return true;
        });

        s1->begin();
        s2->begin();

        REQUIRE(s1->active(FrameID(0, 0)) == false);

        s2->enable(FrameID(0, 0));

        // FIXME
        std::this_thread::sleep_for(std::chrono::milliseconds(TEST_TIMEOUT));

        REQUIRE(s1->active(FrameID(0, 0)) == true);

        ftl::protocol::StreamPacket spkt;
        spkt.timestamp = 1;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = ftl::protocol::Channel::kEndFrame;
        ftl::protocol::DataPacket pkt;
        pkt.bitrate = 10;
        pkt.codec = ftl::protocol::Codec::kJPG;
        pkt.packet_count = 1;

        for (int i=0; i<10; ++i) {
            spkt.timestamp += 10;
            s1->post(spkt, pkt);
        }

        // FIXME
        for(int k = 100; k > 0 && rcount < 9; k--) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        const float delay = static_cast<float>(totalDelay) / static_cast<float>(rcount);
        float margin = 3.33f;
        LOG(INFO) << "AVG DELAY = " << delay << ", (" << rcount << " samples)";
        REQUIRE(delay > 10.0f - margin);
        REQUIRE(delay < 10.0f + margin);
    }

    /*SECTION("handles out-of-order packets") {
        MUTEX mtx;
        std::vector<int64_t> times;
        times.reserve(24);

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

        s2->enable(FrameID(0, 0));

        // TODO: Find better option
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        REQUIRE(s1->active(FrameID(0, 0)) == true);

        ftl::protocol::StreamPacket spkt;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = ftl::protocol::Channel::kEndFrame;
        ftl::protocol::DataPacket pkt;
        pkt.bitrate = 10;
        pkt.codec = ftl::protocol::Codec::kJPG;
        pkt.packet_count = 1;

        spkt.timestamp = 100;
        pkt.packet_count = 2;
        s1->post(spkt, pkt);
        pkt.packet_count = 1;
        spkt.timestamp = 120;
        s1->post(spkt, pkt);
        spkt.timestamp = 110;
        pkt.packet_count = 21;
        s1->post(spkt, pkt);
        spkt.channel = ftl::protocol::Channel::kColour;
        for (int i=0; i < 20; ++i) {
            spkt.timestamp = 110;
            s1->post(spkt, pkt);
        }
        spkt.timestamp = 100;
        s1->post(spkt, pkt);

        // TODO: Find better option
        int k = 15;
        while (--k > 0 && times.size() < 24) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        REQUIRE( times.size() == 24 );

        REQUIRE(times[0] == 100);
        REQUIRE(times[1] == 100);
        REQUIRE(times[2] == 110);
        REQUIRE(times[23] == 120);
    }*/

    p.reset();
    ftl::protocol::reset();
}
