#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/uri.hpp>
#include <ftl/exception.hpp>
#include <ftl/protocol/node.hpp>

using ftl::protocol::FrameID;

// --- Tests -------------------------------------------------------------------

TEST_CASE("TCP Stream", "[net]") {
	std::mutex mtx;

	auto self = ftl::createDummySelf();
	self->listen(ftl::URI("tcp://localhost:0")); 

	auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
	LOG(INFO) << uri;
	auto p = ftl::connectNode(uri);
	p->waitConnection(5);

	SECTION("single enabled packet stream") {
		std::condition_variable cv;
		std::unique_lock<std::mutex> lk(mtx);

		auto s1 = ftl::createStream("ftl://mystream");
		REQUIRE( s1 );

		auto s2 = self->getStream("ftl://mystream");
		REQUIRE( s2 );

		ftl::protocol::Packet rpkt;

		auto h = s2->onPacket([&cv, &rpkt](const ftl::protocol::StreamPacket &spkt, const ftl::protocol::Packet &pkt) {
			rpkt = pkt;
			cv.notify_one();
			return true;
		});

		s1->begin();
		s2->begin();

		s2->enable(FrameID(0, 0));

		// TODO: Find better option
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		ftl::protocol::StreamPacket spkt;
		spkt.streamID = 0;
		spkt.frame_number = 0;
		spkt.channel = ftl::protocol::Channel::kColour;
		ftl::protocol::Packet pkt;
		pkt.bitrate = 10;
		pkt.codec = ftl::protocol::Codec::kJPG;
		pkt.frame_count = 1;
		s1->post(spkt, pkt);

		REQUIRE(cv.wait_for(lk, std::chrono::seconds(5)) == std::cv_status::no_timeout);
		REQUIRE( rpkt.bitrate == 10 );
		REQUIRE( rpkt.codec == ftl::protocol::Codec::kJPG );
		REQUIRE( rpkt.frame_count == 1 );
	}

	p.reset();
	ftl::protocol::reset();
}
