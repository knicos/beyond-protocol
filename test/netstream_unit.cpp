#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/uri.hpp>
#include <ftl/exception.hpp>
#include <ftl/protocol/node.hpp>
#include <ftl/time.hpp>
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
        ++postCount;
    }

    void forceSeen(FrameID id, Channel channel) {
        seen(id, channel);
    }

    StreamPacket lastSpkt;
    std::atomic_int postCount = 0;
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

    SECTION("can increase buffering") {
        int ts_packet_1_ms = 50;
        int ts_packet_2_ms = 100;
        float buffering_increase = 0.1f; // Amount how much to increase buffering
        int margin_ms = 5;

        // Buffering increased between packets, expected delay is:
        //   (ts_packet_2_ms - ts_packet_1_ms) + [increase in buffering]
        // within margin configured above

        auto p = createMockPeer(0);
        fakedata[0] = "";
        send_handshake(*p.get());
        p->recv();
        while (p->jobs() > 0) sleep_for(milliseconds(1));

        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), false);
        
        int64_t prevTs = 0;
        int64_t delta = 0;
        std::atomic_int count = 0;

        auto h = s1->onPacket([&prevTs, &count, &delta](const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
            int64_t now = ftl::time::get_time();
            delta = now - prevTs;
            prevTs = now;
            ++count;
            return true;
        });

        s1->setProperty(ftl::protocol::StreamProperty::kAutoBufferAdjust, false);
        float buffering_old = std::any_cast<float>(s1->getProperty(ftl::protocol::StreamProperty::kBuffering));
        REQUIRE(s1->begin());

        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.timestamp = ts_packet_1_ms;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = Channel::kColour;
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->recv();
        while (p->jobs() > 0) sleep_for(milliseconds(1));

        while (count < 1) {
            // Wait for the packet 1
            sleep_for(milliseconds(10));
        }

        spkt.timestamp = ts_packet_2_ms;
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));

        s1->setProperty(ftl::protocol::StreamProperty::kBuffering, buffering_old + buffering_increase);

        p->recv();
        while (p->jobs() > 0) sleep_for(milliseconds(1));

        while (count < 2) {
            // Wait for packet 2
            sleep_for(milliseconds(10));
        }

        int expected = ts_packet_2_ms - ts_packet_1_ms + buffering_increase*1000;
        REQUIRE(delta > expected - margin_ms);
        REQUIRE(delta < expected + margin_ms);
    }
}

TEST_CASE("Net stream sending requests") {
    auto p = createMockPeer(0);
    fakedata[0] = "";
    send_handshake(*p.get());
    p->recv();
    while (p->jobs() > 0) sleep_for(milliseconds(1));
    fakedata[0] = "";

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

    SECTION("sends repeat requests - single frame") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), false);
        
        // Thread to provide response to otherwise blocking call
        std::thread thr([&p]() {
            auto z = std::make_unique<msgpack::zone>();
            provideResponses(p, 0, {
                {false, "find_stream", packResponse(*z, ftl::UUIDMSGPACK(p->id()))},
                {true, "enable_stream", {}},
            });
        });

        s1->setProperty(StreamProperty::kRequestSize, 10);

        REQUIRE( s1->begin() );

        s1->forceSeen(FrameID(0, 0), Channel::kColour);
        REQUIRE( s1->enable(FrameID(0, 0), Channel::kColour));

        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = Channel::kEndFrame;

        thr.join();

        s1->lastSpkt.channel = Channel::kNone;

        for (int i=0; i<20; ++i) {
            spkt.timestamp = i;
            writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
            p->recv();
            while (p->jobs() > 0) sleep_for(milliseconds(1));
        }

        while (s1->postCount < 4) sleep_for(milliseconds(10));

        REQUIRE( s1->lastSpkt.channel == Channel::kColour );
        REQUIRE( (s1->lastSpkt.flags & ftl::protocol::kFlagRequest) > 0 );
    }

    SECTION("sends repeat requests - multi frame") {
        auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), false);
        
        // Thread to provide response to otherwise blocking call
        std::thread thr([&p]() {
            auto z = std::make_unique<msgpack::zone>();
            provideResponses(p, 0, {
                {false, "find_stream", packResponse(*z, ftl::UUIDMSGPACK(p->id()))},
                {true, "enable_stream", {}},
            });
        });

        s1->setProperty(StreamProperty::kRequestSize, 10);

        REQUIRE( s1->begin() );

        s1->forceSeen(FrameID(0, 0), Channel::kColour);
        s1->forceSeen(FrameID(0, 1), Channel::kColour);
        REQUIRE( s1->enable(FrameID(0, 0), Channel::kColour));
        REQUIRE( s1->enable(FrameID(0, 1), Channel::kColour));

        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = Channel::kEndFrame;

        thr.join();

        s1->lastSpkt.channel = Channel::kNone;

        for (int i=0; i<30; ++i) {
            spkt.frame_number = i & 0x1;
            spkt.timestamp = i >> 1;
            writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
            p->recv();
            while (p->jobs() > 0) sleep_for(milliseconds(1));
        }

        while (s1->postCount < 3) sleep_for(milliseconds(10));

        REQUIRE( s1->lastSpkt.channel == Channel::kColour );
        REQUIRE( (s1->lastSpkt.flags & ftl::protocol::kFlagRequest) > 0 );
        s1->end();
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
        spkt.timestamp = 0;
        spkt.streamID = 1;
        spkt.frame_number = 1;
        spkt.channel = Channel::kColour;
        spkt.flags = ftl::protocol::kFlagRequest;
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->recv();
        while (p->jobs() > 0) sleep_for(milliseconds(1));

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
        p->recv();

        while (p->jobs() > 0) sleep_for(milliseconds(1));
        REQUIRE( bitrate == 100 );

        s1->setProperty(ftl::protocol::StreamProperty::kBitrate, 200);
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->recv();

        while (p->jobs() > 0) sleep_for(milliseconds(1));
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
        p->recv();

        while (p->jobs() > 0) sleep_for(milliseconds(1));
        REQUIRE( seenReq );
    }

    p.reset();
    ftl::protocol::reset();
}

TEST_CASE("Net stream can see received data") {
    auto p = createMockPeer(0);
    fakedata[0] = "";
    send_handshake(*p.get());
    p->recv();
    while (p->jobs() > 0) sleep_for(milliseconds(1));

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
        p->recv();
        while (p->jobs() > 0) sleep_for(milliseconds(1));
        spkt.channel = Channel::kEndFrame;
        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        p->recv();

        while (p->jobs() > 0) sleep_for(milliseconds(1));
        REQUIRE( seenReq );
        REQUIRE( s1->available(FrameID(1, 1), Channel::kColour) );
    }

    p.reset();
    ftl::protocol::reset();
}

TEST_CASE("receive buffering, interleaving and reordering") {
    auto p = createMockPeer(0);
    fakedata[0] = "";
    send_handshake(*p.get());
    mockRecv(p);

    auto s1 = std::make_shared<MockNetStream>("ftl://mystream", ftl::getSelf()->getUniverse(), false);

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<std::tuple<Channel, int64_t>> recv_buffer;

    s1->setProperty(ftl::protocol::StreamProperty::kAutoBufferAdjust, false);
    s1->setProperty(ftl::protocol::StreamProperty::kBuffering, 0.02f);

    auto h = s1->onPacket([&](const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
        {
            auto lk = std::unique_lock(mtx);
            recv_buffer.emplace_back(spkt.channel, spkt.timestamp);
        }
        cv.notify_all();
        return true;
    });

    auto wait_for_packets = [&](size_t expected_size, int timeout_ms=1000) {
        auto lk = std::unique_lock(mtx);
        auto cond = [&](){ return recv_buffer.size() == expected_size; };
        if (cond()) { return true; }
        return cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), cond);
    };

    auto send_packet = [&](int64_t ts) {
        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = Channel::kColour;
        spkt.timestamp = ts;

        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        mockRecv(p);
    };

    auto send_eof = [&](int64_t ts, int count) {
        ftl::protocol::StreamPacketMSGPACK spkt;
        ftl::protocol::PacketMSGPACK pkt;
        spkt.streamID = 0;
        spkt.frame_number = 0;
        spkt.channel = Channel::kEndFrame;
        spkt.timestamp = ts;

        pkt.frame_count = 1;
        pkt.packet_count = count + 1;

        writeNotification(0, "ftl://mystream", std::make_tuple(0, spkt, pkt));
        mockRecv(p);
    };

    auto print_buffer = [&]() {
        for(auto& v : recv_buffer) {
            LOG(INFO) << (int)std::get<0>(v) << ", " <<  (int)std::get<1>(v);
        }
    };

    // Test for incorrect input, stream must still make sure EndOfFrame packets are in the end. 
    // Only with long enough buffer, stream must reorder packets to correct order.

    for (bool interleave : {false, true}) {
        SECTION("kEndOfFrame must always be last packet for each timestmap") {
            LOG(INFO) << "Interleave: " << interleave;
            REQUIRE(s1->begin());
            // Without the waits after every packet, the buffer may receive all packets and (correctly)
            // reorder them before calling the callback.
            s1->allowFrameInterleaving(interleave);
            std::vector<int64_t> timestamps = { 100, 400, 200, 300, 700, 800, 600, 500 };

            int count = 0;
            for (auto ts : timestamps) {
                send_packet(ts);
                send_eof(ts, 1);
                count += 2;
                wait_for_packets(count, 200);
            }

            REQUIRE(recv_buffer.size() == timestamps.size()*2);
            for (auto ts : timestamps) {
                auto itr_data = std::find(recv_buffer.begin(), recv_buffer.end(), 
                    std::make_tuple(Channel::kColour, ts));
                auto itr_eof = std::find(recv_buffer.begin(), recv_buffer.end(),
                    std::make_tuple(Channel::kEndFrame, ts));
                
                REQUIRE(itr_data != recv_buffer.end());
                REQUIRE(itr_eof != recv_buffer.end());
                REQUIRE(itr_data < itr_eof);
            }
        }

        SECTION("kEndOfFrame must always be last packet for each timestmap, first send data, then end of frames") {
            LOG(INFO) << "Interleave: " << interleave;
            REQUIRE(s1->begin());
            // Without the waits after every packet, the buffer may receive all packets and (correctly)
            // reorder them before calling the callback.
            s1->allowFrameInterleaving(interleave);
            std::vector<int64_t> timestamps = { 100, 400, 200, 300, 700, 800, 600, 500 };

            for (auto ts : timestamps) { send_packet(ts); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            int count = timestamps.size();
            for (auto ts : timestamps) { send_eof(ts, 1); wait_for_packets(++count, 200); }
            
            REQUIRE(recv_buffer.size() == timestamps.size()*2);
            for (auto ts : timestamps) {
                auto itr_data = std::find(recv_buffer.begin(), recv_buffer.end(), 
                    std::make_tuple(Channel::kColour, ts));
                auto itr_eof = std::find(recv_buffer.begin(), recv_buffer.end(),
                    std::make_tuple(Channel::kEndFrame, ts));
                
                REQUIRE(itr_data != recv_buffer.end());
                REQUIRE(itr_eof != recv_buffer.end());
                REQUIRE(itr_data < itr_eof);
            }
        }
    }
    
    SECTION("large buffer, frames must be reordered before callback") {
        REQUIRE(s1->begin());
        s1->allowFrameInterleaving(true);
        s1->setProperty(ftl::protocol::StreamProperty::kBuffering, 1.5f);

        std::vector<int64_t> timestamps = { 100, 400, 200, 300, 700, 800, 600, 500 };

        for (auto ts : timestamps) { send_packet(ts); }
        for (auto ts : timestamps) { send_eof(ts, 1); }

        wait_for_packets(timestamps.size()*2, 20000);

        REQUIRE(recv_buffer.size() == timestamps.size()*2);
        for (auto ts : timestamps) {
            auto itr_data = std::find(recv_buffer.begin(), recv_buffer.end(), 
                std::make_tuple(Channel::kColour, ts));
            auto itr_eof = std::find(recv_buffer.begin(), recv_buffer.end(),
                std::make_tuple(Channel::kEndFrame, ts));
            
            REQUIRE(itr_data != recv_buffer.end());
            REQUIRE(itr_eof != recv_buffer.end());
            REQUIRE(itr_data < itr_eof);
        }
        
        int64_t ts_prev = 0;
        for (auto [c, ts] : recv_buffer) {
            REQUIRE(ts >= ts_prev);
            ts_prev = ts;
        }
    }

    SECTION("allowFrameInterleaving(true) and large buffer, frames must be reordered and callbacks only for complete frames") {
        REQUIRE(s1->begin());
        s1->allowFrameInterleaving(false);
        s1->setProperty(ftl::protocol::StreamProperty::kBuffering, 1.5f);

        std::vector<int64_t> timestamps = { 50, 200, 100, 150, 350, 400, 300, 250 };

        for (auto ts : timestamps) { send_packet(ts); }
        wait_for_packets(timestamps.size(), 2000);
        REQUIRE(recv_buffer.size() == 0);

        std::sort(timestamps.begin(), timestamps.end());
        for (auto ts : timestamps) { send_eof(ts, 1); }
        wait_for_packets(timestamps.size()*2, 2000);

        REQUIRE(recv_buffer.size() == timestamps.size()*2);
        for (auto ts : timestamps) {
            auto itr_data = std::find(recv_buffer.begin(), recv_buffer.end(), 
                std::make_tuple(Channel::kColour, ts));
            auto itr_eof = std::find(recv_buffer.begin(), recv_buffer.end(),
                std::make_tuple(Channel::kEndFrame, ts));
            
            REQUIRE(itr_data != recv_buffer.end());
            REQUIRE(itr_eof != recv_buffer.end());
            REQUIRE(itr_data < itr_eof);
        }
        
        int64_t ts_prev = 0;
        for (auto [c, ts] : recv_buffer) {
            REQUIRE(ts >= ts_prev);
            ts_prev = ts;
        }
    }

    // Correct input, packets in arriving correct order must be in correct order in callback

    SECTION("input on correct order must be in same order in callback") {
        REQUIRE(s1->begin());
        s1->allowFrameInterleaving(true);
        s1->setProperty(ftl::protocol::StreamProperty::kBuffering, 1.5f);

        std::vector<int64_t> timestamps = { 10, 20, 40, 60, 70, 80, 100, 120, 200, 201, 202, 203 };

        for (auto ts : timestamps) { send_packet(ts); }
        wait_for_packets(timestamps.size(), 2000);

        std::sort(timestamps.begin(), timestamps.end());
        for (auto ts : timestamps) { send_eof(ts, 1); }
        wait_for_packets(timestamps.size()*2, 2000);

        REQUIRE(recv_buffer.size() == timestamps.size()*2);
        for (auto ts : timestamps) {
            auto itr_data = std::find(recv_buffer.begin(), recv_buffer.end(), 
                std::make_tuple(Channel::kColour, ts));
            auto itr_eof = std::find(recv_buffer.begin(), recv_buffer.end(),
                std::make_tuple(Channel::kEndFrame, ts));
            
            REQUIRE(itr_data != recv_buffer.end());
            REQUIRE(itr_eof != recv_buffer.end());
            REQUIRE(itr_data < itr_eof);
        }
        
        int64_t ts_prev = 0;
        for (auto [c, ts] : recv_buffer) {
            REQUIRE(ts >= ts_prev);
            ts_prev = ts;
        }
    }
}
