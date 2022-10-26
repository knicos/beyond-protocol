#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/uri.hpp>
#include <ftl/exception.hpp>
#include <ftl/protocol/node.hpp>
#include <thread>
#include <chrono>

#include "../src/streams/netstream.hpp"
#include "../src/streams/packetMsgpack.hpp"
#include "../src/uuidMSGPACK.hpp"
#include "mocks/connection.hpp"

using ftl::protocol::FrameID;
using ftl::protocol::StreamProperty;
using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using ftl::protocol::Channel;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;

// --- Mock --------------------------------------------------------------------

class MockNetStream : public ftl::protocol::Net {
    public:
    MockNetStream(const std::string &uri, ftl::net::Universe *net, bool host=false): Net(uri, net, host) {};

    void hasPosted(const StreamPacket &spkt, const DataPacket &pkt) override {
        lastSpkt = spkt;
    }

    void forceSeen(FrameID id, Channel channel) {
        seen(id, channel);
    }

    StreamPacket lastSpkt;
};

// --- Tests -------------------------------------------------------------------

TEST_CASE("Net stream options") {
    SECTION("can get correct URI") {
        auto s1 = ftl::createStream("ftl://mystream?opt=none");
        REQUIRE( s1 );
        REQUIRE( s1->begin() );

        REQUIRE( std::any_cast<std::string>(s1->getProperty(StreamProperty::kURI)) == "ftl://mystream" );
    }

    SECTION("can get a name") {
        auto s1 = ftl::createStream("ftl://mystream?opt=none");
        REQUIRE( s1 );
        REQUIRE( std::any_cast<std::string>(s1->getProperty(StreamProperty::kName)).size() > 0 );
    }

    SECTION("can pause the stream") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), true);
        REQUIRE( s1->begin() );

        StreamPacket spkt;
        spkt.timestamp = 100;
        spkt.streamID = 1;
        spkt.frame_number = 2;
        spkt.channel = Channel::kColour;

        DataPacket pkt;
        pkt.frame_count = 1;

        s1->lastSpkt.timestamp = 0;
        REQUIRE( s1->post(spkt, pkt) );
        REQUIRE( s1->lastSpkt.timestamp == 100 );

        s1->setProperty(StreamProperty::kPaused, true);

        spkt.timestamp = 200;
        REQUIRE( s1->post(spkt, pkt) );
        REQUIRE( s1->lastSpkt.timestamp == 100 );
        REQUIRE( std::any_cast<bool>(s1->getProperty(StreamProperty::kPaused)) );
    }
}

TEST_CASE("Net stream sending requests") {
    auto p = createMockPeer(0);
    fakedata[0] = "";
    send_handshake(*p.get());
    p->data();
    sleep_for(milliseconds(50));

    SECTION("cannot enable if not seen") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), false);
        REQUIRE( s1->begin() );
        REQUIRE( !s1->enable(FrameID(1, 1), Channel::kDepth));
    }

    SECTION("sends request on enable") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), false);
        
        // Thread to provide response to otherwise blocking call
        std::thread thr([&p]() {
            auto z = std::make_unique<msgpack::zone>();
            provideResponses(p, 0, {
                {false, "find_stream", packResponse(*z, ftl::UUIDMSGPACK(p->id()))},
                {true, "enable_stream", {}},
            });
        });

        REQUIRE( s1->begin() );

        s1->forceSeen(FrameID(1, 1), Channel::kDepth);
        s1->lastSpkt.channel = Channel::kNone;
        REQUIRE( s1->enable(FrameID(1, 1), Channel::kDepth));

        thr.join();

        REQUIRE( s1->lastSpkt.streamID == 1 );
        REQUIRE( int(s1->lastSpkt.frame_number) == 1 );  // TODO: update when this is fixed
        REQUIRE( s1->lastSpkt.channel == Channel::kDepth );
        REQUIRE( (s1->lastSpkt.flags & ftl::protocol::kFlagRequest) > 0 );
    }

    SECTION("responds to requests") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), true);
        
        REQUIRE( s1->begin() );

        bool seenReq = false;

        auto h = s1->onRequest([&seenReq](const ftl::protocol::Request &req) {
            seenReq = true;
            return true;
        });

        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.streamID = 1;
        spkt.frame_number = 1;
        spkt.channel = Channel::kColour;
        spkt.flags = ftl::protocol::kFlagRequest;
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->data();

        sleep_for(milliseconds(50));
        REQUIRE( seenReq );
    }

    SECTION("adjusts request bitrate") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), true);
        
        REQUIRE( s1->begin() );

        int bitrate = 255;

        auto h = s1->onRequest([&bitrate](const ftl::protocol::Request &req) {
            bitrate = req.bitrate;
            return true;
        });

        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.streamID = 1;
        spkt.frame_number = 1;
        spkt.channel = Channel::kColour;
        spkt.flags = ftl::protocol::kFlagRequest;
        pkt.bitrate = 255;
        s1->setProperty(ftl::protocol::StreamProperty::kBitrate, 100);
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->data();

        sleep_for(milliseconds(50));
        REQUIRE( bitrate == 100 );

        s1->setProperty(ftl::protocol::StreamProperty::kBitrate, 200);
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->data();

        sleep_for(milliseconds(50));
        REQUIRE( bitrate == 200 );
    }

    SECTION("responds to 255 requests") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), true);
        
        REQUIRE( s1->begin() );
        s1->seen(FrameID(1, 0), Channel::kEndFrame);

        bool seenReq = false;

        auto h = s1->onRequest([&seenReq](const ftl::protocol::Request &req) {
            if (req.id.frameset() == 1) seenReq = true;
            return true;
        });

        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.streamID = 255;
        spkt.frame_number = 255;
        spkt.channel = Channel::kColour;
        spkt.flags = ftl::protocol::kFlagRequest;
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->data();

        sleep_for(milliseconds(50));
        REQUIRE( seenReq );
    }

    p.reset();
    ftl::protocol::reset();
}

TEST_CASE("Net stream can see received data") {
    auto p = createMockPeer(0);
    fakedata[0] = "";
    send_handshake(*p.get());
    p->data();
    sleep_for(milliseconds(50));

    SECTION("available if packet is seen") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), true);
        
        REQUIRE( s1->begin() );

        bool seenReq = false;

        auto h = s1->onAvailable([&seenReq](FrameID id, Channel channel) {
            seenReq = true;
            return true;
        });

        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.streamID = 1;
        spkt.frame_number = 1;
        spkt.channel = Channel::kColour;
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->data();
        spkt.channel = Channel::kEndFrame;
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->data();

        sleep_for(milliseconds(50));
        REQUIRE( seenReq );
        REQUIRE( s1->available(FrameID(1, 1), Channel::kColour) );
    }

    p.reset();
    ftl::protocol::reset();
}
