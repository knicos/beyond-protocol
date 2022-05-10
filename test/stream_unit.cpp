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

	private:
	//std::function<void(const StreamPacket &, const Packet &)> cb_;
};

TEST_CASE("ftl::stream::Muxer()::write", "[stream]") {

	std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
	REQUIRE(mux);

	SECTION("write with one stream") {
		std::shared_ptr<Stream> s = std::make_shared<TestStream>();
		REQUIRE(s);

		mux->add(s);

		ftl::protocol::StreamPacket tspkt = {4,0,0,1, Channel::kColour};

		auto h = s->onPacket([&tspkt](const StreamPacket &spkt, const Packet &pkt) {
			tspkt = spkt;
			return true;
		});

		REQUIRE( !mux->post({4,100,0,1,ftl::protocol::Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 0 );
	}

	SECTION("write to previously read") {

		std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
		REQUIRE(s1);
		std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
		REQUIRE(s2);

		mux->add(s1);
		mux->add(s2);

		ftl::protocol::StreamPacket tspkt = {4,0,0,1,Channel::kColour};
		auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const Packet &pkt) {
			tspkt = spkt;
			return true;
		});

		REQUIRE( s1->post({4,100,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.streamID == 0 );
		REQUIRE( tspkt.timestamp == 100 );
		REQUIRE( tspkt.frame_number == 0 );

		REQUIRE( s2->post({4,101,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.streamID == 1 );
		REQUIRE( tspkt.timestamp == 101 );
		REQUIRE( tspkt.frame_number == 0 );

		StreamPacket tspkt2 = {4,0,0,1,Channel::kColour};
		StreamPacket tspkt3 = {4,0,0,1,Channel::kColour};
		auto h2 = s1->onPacket([&tspkt2](const StreamPacket &spkt, const Packet &pkt) {
			tspkt2 = spkt;
			return true;
		});
		auto h3 = s2->onPacket([&tspkt3](const StreamPacket &spkt, const Packet &pkt) {
			tspkt3 = spkt;
			return true;
		});

		REQUIRE( mux->post({4,200,1,0,Channel::kColour},{}) );
		REQUIRE( tspkt3.timestamp == 200 );
		REQUIRE( tspkt3.streamID == 0 );
		REQUIRE( tspkt3.frame_number == 0 );
	}
}

TEST_CASE("ftl::stream::Muxer()::post multi-frameset", "[stream]") {

	std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
	REQUIRE(mux);

	SECTION("write to previously read") {
		std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
		REQUIRE(s1);
		std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
		REQUIRE(s2);

		mux->add(s1);
		mux->add(s2,1);

		StreamPacket tspkt = {4,0,0,1,Channel::kColour};
		auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const Packet &pkt) {
			tspkt = spkt;
			return true;
		});

		REQUIRE( s1->post({4,100,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.streamID == 0 );
		REQUIRE( tspkt.frame_number == 0 );

		REQUIRE( s2->post({4,101,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.streamID == 1 );
		REQUIRE( tspkt.frame_number == 0 );

		StreamPacket tspkt2 = {4,0,0,1,Channel::kColour};
		StreamPacket tspkt3 = {4,0,0,1,Channel::kColour};
		auto h2 = s1->onPacket([&tspkt2](const StreamPacket &spkt, const Packet &pkt) {
			tspkt2 = spkt;
			return true;
		});
		auto h3 = s2->onPacket([&tspkt3](const StreamPacket &spkt, const Packet &pkt) {
			tspkt3 = spkt;
			return true;
		});

		REQUIRE( mux->post({4,200,1,0,Channel::kColour},{}) );
		REQUIRE( tspkt3.streamID == 0 );
		REQUIRE( tspkt3.frame_number == 0 );
	}
}

TEST_CASE("ftl::stream::Muxer()::read", "[stream]") {
	std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
	REQUIRE(mux);

	SECTION("read with two writing streams") {
		std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
		REQUIRE(s1);
		std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
		REQUIRE(s2);

		mux->add(s1, 0);
		mux->add(s2, 0);

		StreamPacket tspkt = {4,0,0,1,Channel::kColour};
		auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const Packet &pkt) {
			tspkt = spkt;
			return true;
		});

		REQUIRE( s1->post({4,100,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 100 );
		REQUIRE( tspkt.frame_number == 0 );

		REQUIRE( s2->post({4,101,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 101 );
		REQUIRE( tspkt.frame_number == 1 );

		REQUIRE( s1->post({4,102,0,1,Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 102 );
		REQUIRE( tspkt.frame_number == 2 );

		REQUIRE( s2->post({4,103,0,1,Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 103 );
		REQUIRE( tspkt.frame_number == 3 );
	}

	SECTION("read consistency with two writing streams") {
		std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
		REQUIRE(s1);
		std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
		REQUIRE(s2);

		mux->add(s1, 0);
		mux->add(s2, 0);

		StreamPacket tspkt = {4,0,0,1,Channel::kColour};
		auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const Packet &pkt) {
			tspkt = spkt;
			return true;
		});

		REQUIRE( s1->post({4,100,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 100 );
		REQUIRE( tspkt.frame_number == 0 );

		REQUIRE( s2->post({4,101,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 101 );
		REQUIRE( tspkt.frame_number == 1 );

		REQUIRE( s1->post({4,102,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 102 );
		REQUIRE( tspkt.frame_number == 0 );

		REQUIRE( s2->post({4,103,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.timestamp == 103 );
		REQUIRE( tspkt.frame_number == 1 );
	}
}

TEST_CASE("ftl::stream::Muxer()::read multi-frameset", "[stream]") {
	std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
	REQUIRE(mux);

	//SECTION("read with two writing streams") {

		std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
		REQUIRE(s1);
		std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
		REQUIRE(s2);
		std::shared_ptr<Stream> s3 = std::make_shared<TestStream>();
		REQUIRE(s3);
		std::shared_ptr<Stream> s4 = std::make_shared<TestStream>();
		REQUIRE(s4);

		mux->add(s1,0);
		mux->add(s2,1);
		mux->add(s3,0);
		mux->add(s4,1);

		StreamPacket tspkt = {4,0,0,1,Channel::kColour};
		auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const Packet &pkt) {
			tspkt = spkt;
			return true;
		});

		REQUIRE( s1->post({4,100,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.streamID == 0 );
		REQUIRE( tspkt.frame_number == 0 );

		REQUIRE( s2->post({4,101,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.streamID == 1 );
		REQUIRE( tspkt.frame_number == 0 );

		REQUIRE( s3->post({4,102,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.streamID == 0 );
		REQUIRE( tspkt.frame_number == 1 );

		REQUIRE( s4->post({4,103,0,0,Channel::kColour},{}) );
		REQUIRE( tspkt.streamID == 1 );
		REQUIRE( tspkt.frame_number == 1 );
	//}
}

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
