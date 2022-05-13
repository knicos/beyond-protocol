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

		ftl::protocol::Packet rpkt;
		rpkt.bitrate = 20;

		auto h = s2->onPacket([&cv, &rpkt](const ftl::protocol::StreamPacket &spkt, const ftl::protocol::Packet &pkt) {
			rpkt = pkt;
			cv.notify_one();
			return true;
		});

		bool seenReq = false;
		auto h2 = s1->onRequest([&seenReq](const ftl::protocol::Request &req) {
			seenReq = true;
			return true;
		});

		s1->begin();
		s2->begin();

		s2->enable(FrameID(0, 0));

		// TODO: Find better option
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		REQUIRE( seenReq );

		ftl::protocol::StreamPacket spkt;
		spkt.streamID = 0;
		spkt.frame_number = 0;
		spkt.channel = ftl::protocol::Channel::kColour;
		ftl::protocol::Packet pkt;
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

	p.reset();
	ftl::protocol::reset();
}
