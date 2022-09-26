#include "catch.hpp"
#include "../src/streams/packetmanager.hpp"

using ftl::PacketManager;
using ftl::protocol::Channel;
using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using ftl::protocol::PacketPair;

static ftl::protocol::PacketPair makePair(int64_t ts, Channel c)  {
    StreamPacket spkt;
    spkt.timestamp = ts;
    spkt.streamID = 0;
    spkt.channel = c;
    spkt.frame_number = 0;

    DataPacket pkt;
    return {spkt, pkt};
}

TEST_CASE( "PacketManager multiple in-order frames" ) {
	PacketManager mgr;

    int count = 0;

    PacketPair p;
    p = makePair(100, Channel::kColour);
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(100, Channel::kPose);
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(100, Channel::kEndFrame);
    p.second.packet_count = 3;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(101, Channel::kPose);
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    REQUIRE(count == 4);
}

TEST_CASE( "PacketManager out-of-order frames" ) {
	PacketManager mgr;

    int count = 0;

    PacketPair p;
    p = makePair(200, Channel::kColour);
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(201, Channel::kPose);
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    REQUIRE(count == 1);

    p = makePair(200, Channel::kEndFrame);
    p.second.packet_count = 2;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    REQUIRE(count == 3);
}

TEST_CASE( "PacketManager many out-of-order frames" ) {
	PacketManager mgr;

    std::vector<int64_t> times;

    PacketPair p;
    p = makePair(300, Channel::kColour);
    mgr.submit(p, [&times](const PacketPair &pp) {
        times.push_back(pp.first.timestamp);
    });

    p = makePair(301, Channel::kPose);
    mgr.submit(p, [&times](const PacketPair &pp) {
        times.push_back(pp.first.timestamp);
    });

    p = makePair(302, Channel::kPose);
    mgr.submit(p, [&times](const PacketPair &pp) {
        times.push_back(pp.first.timestamp);
    });

    p = makePair(301, Channel::kDepth);
    mgr.submit(p, [&times](const PacketPair &pp) {
        times.push_back(pp.first.timestamp);
    });

    REQUIRE(times.size() == 1);

    p = makePair(300, Channel::kEndFrame);
    p.second.packet_count = 2;
    mgr.submit(p, [&times](const PacketPair &pp) {
        times.push_back(pp.first.timestamp);
    });

    REQUIRE(times.size() == 4);

    p = makePair(301, Channel::kEndFrame);
    p.second.packet_count = 3;
    mgr.submit(p, [&times](const PacketPair &pp) {
        times.push_back(pp.first.timestamp);
    });

    REQUIRE(times.size() == 6);

    REQUIRE(times[0] == 300);
    REQUIRE(times[1] == 300);
    REQUIRE(times[2] == 301);
    REQUIRE(times[3] == 301);
    REQUIRE(times[4] == 301);
    REQUIRE(times[5] == 302);
}

TEST_CASE( "Incomplete frames" ) {
	PacketManager mgr;

    int count = 0;

    PacketPair p;

    p = makePair(400, Channel::kEndFrame);
    p.second.packet_count = 2;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(401, Channel::kEndFrame);
    p.second.packet_count = 1;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(402, Channel::kEndFrame);
    p.second.packet_count = 1;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    REQUIRE(count == 1);

    p = makePair(403, Channel::kEndFrame);
    p.second.packet_count = 1;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(404, Channel::kEndFrame);
    p.second.packet_count = 1;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(405, Channel::kEndFrame);
    p.second.packet_count = 1;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(406, Channel::kEndFrame);
    p.second.packet_count = 1;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    REQUIRE(count == 7);
}

TEST_CASE( "Overflow the buffer" ) {
	PacketManager mgr;

    int count = 0;

    PacketPair p;

    p = makePair(400, Channel::kEndFrame);
    p.second.packet_count = 2;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(401, Channel::kColour);
    for (int i = 0; i<95; ++i) {
        mgr.submit(p, [&count](const PacketPair &pp) {
            ++count;
        });
    }

    p = makePair(400, Channel::kColour);
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    p = makePair(402, Channel::kColour);
    for (int i = 0; i<95; ++i) {
        mgr.submit(p, [&count](const PacketPair &pp) {
            ++count;
        });
    }

    p = makePair(401, Channel::kEndFrame);
    p.second.packet_count = 96;
    mgr.submit(p, [&count](const PacketPair &pp) {
        ++count;
    });

    REQUIRE(count == 96 + 95 + 2);
}
