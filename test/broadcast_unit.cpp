#include "catch.hpp"

#include <ftl/protocol/streams.hpp>
#include <ftl/protocol/muxer.hpp>
#include <ftl/protocol/broadcaster.hpp>
#include <nlohmann/json.hpp>

using ftl::protocol::Muxer;
using ftl::protocol::Broadcast;
using ftl::protocol::Stream;
using ftl::protocol::StreamPacket;
using ftl::protocol::Packet;
using ftl::protocol::Channel;
using ftl::protocol::ChannelSet;
using ftl::protocol::FrameID;

class TestStream : public ftl::protocol::Stream {
	public:
	TestStream() {};
	~TestStream() {};

	bool post(const ftl::protocol::StreamPacket &spkt, const ftl::protocol::Packet &pkt) {
		seen(FrameID(spkt.streamID, spkt.frame_number), spkt.channel);
		trigger(spkt, pkt);
		return true;
	}

	bool begin() override { return true; }
	bool end() override { return true; }
	bool active() override { return true; }

	void setProperty(ftl::protocol::StreamProperty opt, int value) override {}

	int getProperty(ftl::protocol::StreamProperty opt) override { return 0; }

	bool supportsProperty(ftl::protocol::StreamProperty opt) override { return true; }

	void forceSeen(FrameID id, Channel channel) {
        seen(id, channel);
    }
};

TEST_CASE("ftl::stream::Broadcast()::write", "[stream]") {
	std::unique_ptr<Broadcast> mux = std::make_unique<Broadcast>();
	REQUIRE(mux);

	SECTION("write with two streams") {
		std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
		REQUIRE(s1);
		std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
		REQUIRE(s2);

		mux->add(s1);
		mux->add(s2);

		StreamPacket tspkt1 = {4,0,0,1,Channel::kColour};
		StreamPacket tspkt2 = {4,0,0,1,Channel::kColour};

		auto h1 = s1->onPacket([&tspkt1](const StreamPacket &spkt, const Packet &pkt) {
			tspkt1 = spkt;
			return true;
		});
		auto h2 = s2->onPacket([&tspkt2](const StreamPacket &spkt, const Packet &pkt) {
			tspkt2 = spkt;
			return true;
		});

		REQUIRE( mux->post({4,100,0,1,Channel::kColour},{}) );
		REQUIRE( tspkt1.timestamp == 100 );
		REQUIRE( tspkt2.timestamp == 100 );
	}

}

TEST_CASE("Broadcast enable", "[stream]") {
	std::unique_ptr<Broadcast> mux = std::make_unique<Broadcast>();
	REQUIRE(mux);

    std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
    REQUIRE(s1);
    std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
    REQUIRE(s2);

    mux->add(s1);
	mux->add(s2);

    SECTION("enable frame id") {
        FrameID id1(0, 1);
        s1->forceSeen(id1, Channel::kColour);
		// s2->forceSeen(id1, Channel::kColour);

        REQUIRE( !s1->enabled(id1) );
        REQUIRE( mux->enable(id1) );
        REQUIRE( s1->enabled(id1) );
        REQUIRE( s2->enabled(id1) );

        FrameID id2(1, 1);
        s2->forceSeen(id2, Channel::kColour);

        REQUIRE( !s2->enabled(id2) );
        REQUIRE( mux->enable(id2) );
        REQUIRE( s2->enabled(id2) );
        REQUIRE( s1->enabled(id2) );

        auto frames = mux->enabled();
        REQUIRE( frames.size() == 2 );
        REQUIRE( frames.find(id1) != frames.end() );
        REQUIRE( frames.find(id2) != frames.end() );
    }

    SECTION("enable frame id for unseen") {
        FrameID id(0, 1);
        REQUIRE( mux->enable(id) );
		REQUIRE( s1->enabled(id) );
		REQUIRE( s2->enabled(id) );
    }

    SECTION("enable channel for unseen") {
        FrameID id(0, 1);
        REQUIRE( mux->enable(id, Channel::kDepth) );
		REQUIRE( s1->enabled(id, Channel::kDepth) );
		REQUIRE( s2->enabled(id, Channel::kDepth) );
    }

    SECTION("enable channel set for unseen") {
        FrameID id(0, 1);
        ChannelSet set = {Channel::kDepth, Channel::kRight};
        REQUIRE( mux->enable(id, set) );
    }

    SECTION("enable frame id and channel") {
        FrameID id1(0, 1);
        s1->forceSeen(id1, Channel::kDepth);

        REQUIRE( !s1->enabled(id1, Channel::kDepth) );
        REQUIRE( mux->enable(id1, Channel::kDepth) );
        REQUIRE( s1->enabled(id1, Channel::kDepth) );
        REQUIRE( s2->enabled(id1, Channel::kDepth) );
    }

    SECTION("enable frame id and channel set") {
        FrameID id1(0, 1);
        s1->forceSeen(id1, Channel::kDepth);
        s1->forceSeen(id1, Channel::kRight);

        ChannelSet set = {Channel::kDepth, Channel::kRight};
        REQUIRE( !s1->enabled(id1, Channel::kDepth) );
        REQUIRE( !s1->enabled(id1, Channel::kRight) );
        REQUIRE( mux->enable(id1, set) );
        REQUIRE( s1->enabled(id1, Channel::kDepth) );
        REQUIRE( s1->enabled(id1, Channel::kRight) );
        REQUIRE( s2->enabled(id1, Channel::kDepth) );
    }
}
