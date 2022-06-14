#include "catch.hpp"

#include <ftl/protocol/streams.hpp>
#include <ftl/protocol/muxer.hpp>
#include <ftl/protocol/broadcaster.hpp>
#include <nlohmann/json.hpp>

using ftl::protocol::Muxer;
using ftl::protocol::Broadcast;
using ftl::protocol::Stream;
using ftl::protocol::StreamPacket;
using ftl::protocol::DataPacket;
using ftl::protocol::Channel;
using ftl::protocol::ChannelSet;
using ftl::protocol::FrameID;

class TestStream : public ftl::protocol::Stream {
    public:
    TestStream() {}
    explicit TestStream(const std::string &uri) : uri_(uri) {}
    ~TestStream() {}

    bool post(const ftl::protocol::StreamPacket &spkt, const ftl::protocol::DataPacket &pkt) {
        seen(FrameID(spkt.streamID, spkt.frame_number), spkt.channel);
        trigger(spkt, pkt);
        return true;
    }

    bool begin() override { return true; }
    bool end() override { return true; }
    bool active() override { return true; }

    void setProperty(ftl::protocol::StreamProperty opt, std::any value) override {}

    std::any getProperty(ftl::protocol::StreamProperty opt) override {
        if (opt == ftl::protocol::StreamProperty::kURI) {
            return uri_;
        }
        return 0;
    }

    void sendRequest(Channel c, uint8_t frameset, uint8_t frames, uint8_t count) {
        ftl::protocol::Request req;
        req.id = FrameID(frameset, frames);
        req.channel = c;
        req.bitrate = 255;
        req.codec = ftl::protocol::Codec::kAny;
        req.count = count;
        request(req);
    }

    bool supportsProperty(ftl::protocol::StreamProperty opt) override { return true; }

    void forceSeen(FrameID id, Channel channel) {
        seen(id, channel);
    }

    void fakeError(ftl::protocol::Error err, const std::string &str) {
        error(err, str);
    }

 private:
    std::string uri_;
};

TEST_CASE("Muxer post, distinct framesets", "[stream]") {

    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    SECTION("write with one stream fails") {
        std::shared_ptr<Stream> s = std::make_shared<TestStream>();
        REQUIRE(s);

        mux->add(s);

        ftl::protocol::StreamPacket tspkt = {4,0,0,1, Channel::kColour};

        auto h = s->onPacket([&tspkt](const StreamPacket &spkt, const DataPacket &pkt) {
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
        auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const DataPacket &pkt) {
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
        auto h2 = s1->onPacket([&tspkt2](const StreamPacket &spkt, const DataPacket &pkt) {
            tspkt2 = spkt;
            return true;
        });
        auto h3 = s2->onPacket([&tspkt3](const StreamPacket &spkt, const DataPacket &pkt) {
            tspkt3 = spkt;
            return true;
        });

        REQUIRE( mux->post({4,200,1,0,Channel::kColour},{}) );
        REQUIRE( tspkt3.timestamp == 200 );
        REQUIRE( tspkt3.streamID == 0 );
        REQUIRE( tspkt3.frame_number == 0 );
    }
}

TEST_CASE("Muxer post, single frameset", "[stream]") {

    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    SECTION("write to previously read") {
        std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
        REQUIRE(s1);
        std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        StreamPacket tspkt = {4,0,0,1,Channel::kColour};
        auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const DataPacket &pkt) {
            tspkt = spkt;
            return true;
        });

        REQUIRE( s1->post({4,100,0,0,Channel::kColour},{}) );
        REQUIRE( tspkt.streamID == 1 );
        REQUIRE( tspkt.frame_number == 0 );

        REQUIRE( s2->post({4,101,0,0,Channel::kColour},{}) );
        REQUIRE( tspkt.streamID == 1 );
        REQUIRE( tspkt.frame_number == 1 );

        StreamPacket tspkt2 = {4,0,4,4,Channel::kColour};
        StreamPacket tspkt3 = {4,0,4,4,Channel::kColour};
        auto h2 = s1->onPacket([&tspkt2](const StreamPacket &spkt, const DataPacket &pkt) {
            tspkt2 = spkt;
            return true;
        });
        auto h3 = s2->onPacket([&tspkt3](const StreamPacket &spkt, const DataPacket &pkt) {
            tspkt3 = spkt;
            return true;
        });

        REQUIRE( mux->post({4,200,1,1,Channel::kColour},{}) );
        REQUIRE( tspkt2.streamID == 4 );
        REQUIRE( tspkt2.frame_number == 4 );
        REQUIRE( tspkt3.streamID == 0 );
        REQUIRE( tspkt3.frame_number == 0 );

        REQUIRE( mux->post({4,200,1,0,Channel::kColour},{}) );
        REQUIRE( tspkt2.streamID == 0 );
        REQUIRE( tspkt2.frame_number == 0 );
    }
}

TEST_CASE("Muxer read", "[stream]") {
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
        auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const DataPacket &pkt) {
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
        auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const DataPacket &pkt) {
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

TEST_CASE("Muxer read multi-frameset", "[stream]") {
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
        auto h = mux->onPacket([&tspkt](const StreamPacket &spkt, const DataPacket &pkt) {
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

TEST_CASE("Muxer enable", "[stream]") {
    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
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

        REQUIRE( !s1->enabled(id1) );
        REQUIRE( mux->enable(id1) );
        REQUIRE( s1->enabled(id1) );
        REQUIRE( !s2->enabled(id1) );

        FrameID id2(1, 1);
        s2->forceSeen(id2, Channel::kColour);

        REQUIRE( !s2->enabled(id2) );
        REQUIRE( mux->enable(id2) );
        REQUIRE( s2->enabled(id2) );
        REQUIRE( !s1->enabled(id2) );

        auto frames = mux->enabled();
        REQUIRE( frames.size() == 2 );
        REQUIRE( frames.find(id1) != frames.end() );
        REQUIRE( frames.find(id2) != frames.end() );
    }

    SECTION("enable frame id 255") {
        FrameID id1(0, 1);
        FrameID id2(0, 2);
        FrameID id3(0, 3);
        s1->forceSeen(id1, Channel::kColour);
        s1->forceSeen(id2, Channel::kColour);
        s1->forceSeen(id3, Channel::kColour);

        REQUIRE( !s1->enabled(id1) );
        REQUIRE( !s1->enabled(id2) );
        REQUIRE( !s1->enabled(id3) );
        REQUIRE( mux->enable(FrameID(0, 255)) );
        REQUIRE( s1->enabled(id1) );
        REQUIRE( s1->enabled(id2) );
        REQUIRE( s1->enabled(id3) );
    }

    SECTION("enable frame id fails for unseen") {
        FrameID id(0, 1);
        REQUIRE( !mux->enable(id) );
    }

    SECTION("enable channel fails for unseen") {
        FrameID id(0, 1);
        REQUIRE( !mux->enable(id, Channel::kDepth) );
    }

    SECTION("enable channel set fails for unseen") {
        FrameID id(0, 1);
        ChannelSet set = {Channel::kDepth, Channel::kRight};
        REQUIRE( !mux->enable(id, set) );
    }

    SECTION("enable frame id and channel") {
        FrameID id1(0, 1);
        s1->forceSeen(id1, Channel::kDepth);

        REQUIRE( !s1->enabled(id1, Channel::kDepth) );
        REQUIRE( mux->enable(id1, Channel::kDepth) );
        REQUIRE( s1->enabled(id1, Channel::kDepth) );
        REQUIRE( !s2->enabled(id1, Channel::kDepth) );
    }

    SECTION("enable frame id 255 and channel") {
        FrameID id1(0, 1);
        FrameID id2(0, 2);
        FrameID id3(0, 3);
        s1->forceSeen(id1, Channel::kColour);
        s1->forceSeen(id2, Channel::kColour);
        s1->forceSeen(id3, Channel::kColour);

        REQUIRE( !s1->enabled(id1) );
        REQUIRE( !s1->enabled(id2) );
        REQUIRE( !s1->enabled(id3) );
        REQUIRE( mux->enable(FrameID(0, 255), Channel::kColour) );
        REQUIRE( s1->enabled(id1, Channel::kColour) );
        REQUIRE( s1->enabled(id2, Channel::kColour) );
        REQUIRE( s1->enabled(id3, Channel::kColour) );
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
        REQUIRE( !s2->enabled(id1, Channel::kDepth) );
    }
}

TEST_CASE("Muxer disable", "[stream]") {
    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
    REQUIRE(s1);
    std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
    REQUIRE(s2);

    mux->add(s1);
    mux->add(s2);

    SECTION("disable frame id") {
        FrameID id1(0, 1);
        s1->forceSeen(id1, Channel::kColour);
        FrameID id2(1, 1);
        s2->forceSeen(id2, Channel::kColour);

        REQUIRE( mux->enable(id1) );
        REQUIRE( mux->enable(id2) );
        REQUIRE(s1->enabled(id1));
        REQUIRE(s2->enabled(id2));
        mux->disable(id1);
        REQUIRE(!s1->enabled(id1));
        REQUIRE(s2->enabled(id2));
        mux->disable(id2);
        REQUIRE(!s1->enabled(id1));
        REQUIRE(!s2->enabled(id2));
    }

    SECTION("disable frame channel") {
        FrameID id1(0, 1);
        s1->forceSeen(id1, Channel::kColour);
        s1->forceSeen(id1, Channel::kDepth);

        REQUIRE( mux->enable(id1, Channel::kColour) );
        REQUIRE( mux->enable(id1, Channel::kDepth) );
        REQUIRE(s1->enabled(id1, Channel::kColour));
        REQUIRE(s1->enabled(id1, Channel::kDepth));
        mux->disable(id1, Channel::kColour);
        REQUIRE(!s1->enabled(id1, Channel::kColour));
        REQUIRE(s1->enabled(id1, Channel::kDepth));
    }
}

TEST_CASE("Muxer available", "[stream]") {
    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
    REQUIRE(s1);
    std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
    REQUIRE(s2);

    mux->add(s1);
    mux->add(s2);

    SECTION("available id when seen") {
        FrameID id1(0, 1);
        REQUIRE( !s1->available(id1) );
        REQUIRE( !mux->available(id1) );
        s1->forceSeen(id1, Channel::kColour);
        REQUIRE( s1->available(id1) );
        REQUIRE( mux->available(id1) );

        FrameID id2(1, 1);
        REQUIRE( !s2->available(id2) );
        REQUIRE( !mux->available(id2) );
        s2->forceSeen(id2, Channel::kColour);
        REQUIRE( s2->available(id2) );
        REQUIRE( !s1->available(id2) );
        REQUIRE( mux->available(id1) );
        REQUIRE( mux->available(id2) );
    }

    SECTION("available channel when seen") {
        FrameID id1(0, 1);
        REQUIRE( !s1->available(id1, Channel::kColour) );
        REQUIRE( !mux->available(id1, Channel::kColour) );
        s1->forceSeen(id1, Channel::kColour);
        REQUIRE( s1->available(id1, Channel::kColour) );
        REQUIRE( mux->available(id1, Channel::kColour) );
    }

    SECTION("not available when wrong channel seen") {
        FrameID id1(0, 1);
        s1->forceSeen(id1, Channel::kDepth);
        REQUIRE( mux->available(id1) );
        REQUIRE( !s1->available(id1, Channel::kColour) );
        REQUIRE( !mux->available(id1, Channel::kColour) );
    }

    SECTION("available channel set when seen all") {
        FrameID id1(0, 1);
        ChannelSet set = {Channel::kColour, Channel::kDepth};
        REQUIRE( !s1->available(id1, set) );
        REQUIRE( !mux->available(id1, set) );
        s1->forceSeen(id1, Channel::kColour);
        s1->forceSeen(id1, Channel::kDepth);
        REQUIRE( s1->available(id1, set) );
        REQUIRE( mux->available(id1, set) );
    }

    SECTION("not available channel set if not all seen") {
        FrameID id1(0, 1);
        ChannelSet set = {Channel::kColour, Channel::kDepth};
        REQUIRE( !s1->available(id1, set) );
        REQUIRE( !mux->available(id1, set) );
        s1->forceSeen(id1, Channel::kDepth);
        REQUIRE( !s1->available(id1, set) );
        REQUIRE( !mux->available(id1, set) );
    }
}

TEST_CASE("Muxer onAvailable", "[stream]") {
    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
    REQUIRE(s1);
    std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
    REQUIRE(s2);

    mux->add(s1);
    mux->add(s2);

    SECTION("available event when seen") {
        FrameID id1(0, 1);

        bool seen1a = false;
        auto h1 = s1->onAvailable([&seen1a, id1](FrameID id, Channel channel) {
            seen1a = true;
            REQUIRE( id == id1 );
            return true;
        });

        bool seen1b = false;
        auto h2 = mux->onAvailable([&seen1b, id1](FrameID id, Channel channel) {
            seen1b = true;
            REQUIRE( id == id1 );
            return true;
        });

        s1->forceSeen(id1, Channel::kColour);
        REQUIRE( seen1a );
        REQUIRE( seen1b );        
    }
}

TEST_CASE("Muxer frames", "[stream]") {
    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
    REQUIRE(s1);
    std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
    REQUIRE(s2);

    SECTION("unique framesets list correct") {
        mux->add(s1);
        mux->add(s2);

        FrameID id1(0, 1);
        FrameID id2(1, 1);
        FrameID id3(0, 2);
        FrameID id4(1, 2);

        REQUIRE( mux->frames().size() == 0 );
        
        s1->forceSeen(id1, Channel::kColour);
        s2->forceSeen(id2, Channel::kColour);
        s1->forceSeen(id3, Channel::kColour);
        s2->forceSeen(id4, Channel::kColour);

        auto frames = mux->frames();
        REQUIRE( frames.size() == 4 );
        REQUIRE( frames.find(id1) != frames.end() );
        REQUIRE( frames.find(id2) != frames.end() );
        REQUIRE( frames.find(id3) != frames.end() );
        REQUIRE( frames.find(id4) != frames.end() );
    }

    SECTION("merged framesets list correct") {
        mux->add(s1, 1);
        mux->add(s2, 1);

        FrameID id1(0, 0);
        FrameID id2(0, 1);

        REQUIRE( mux->frames().size() == 0 );
        
        s1->forceSeen(id1, Channel::kColour);
        s2->forceSeen(id1, Channel::kColour);
        s1->forceSeen(id2, Channel::kColour);
        s2->forceSeen(id2, Channel::kColour);

        auto frames = mux->frames();
        REQUIRE( frames.size() == 4 );
        REQUIRE( frames.find(FrameID(1, 0)) != frames.end() );
        REQUIRE( frames.find(FrameID(1, 1)) != frames.end() );
        REQUIRE( frames.find(FrameID(1, 2)) != frames.end() );
        REQUIRE( frames.find(FrameID(1, 3)) != frames.end() );
    }
}

TEST_CASE("Muxer channels", "[stream]") {
    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
    REQUIRE(s1);
    std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
    REQUIRE(s2);

    mux->add(s1);
    mux->add(s2);

    SECTION("correct channels for valid frame") {
        FrameID id1(0, 1);

        s1->forceSeen(id1, Channel::kColour);
        s1->forceSeen(id1, Channel::kDepth);

        auto set = mux->channels(id1);
        REQUIRE( set.size() == 2 );
        REQUIRE( set.count(Channel::kColour) == 1 );
        REQUIRE( set.count(Channel::kDepth) == 1 );
    }

    SECTION("empty for invalid frame") {
        FrameID id1(0, 1);
        auto set = mux->channels(id1);
        REQUIRE( set.size() == 0 );
    }
}

TEST_CASE("Muxer enabledChannels", "[stream]") {
    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
    REQUIRE(s1);
    std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
    REQUIRE(s2);

    mux->add(s1);
    mux->add(s2);

    SECTION("correct channels for valid frame") {
        FrameID id1(0, 1);

        s1->forceSeen(id1, Channel::kColour);
        s1->forceSeen(id1, Channel::kDepth);

        auto set = mux->channels(id1);
        REQUIRE( set.size() == 2 );
        
        REQUIRE( mux->enable(id1, set) );

        REQUIRE( mux->enabledChannels(id1) == set );
        REQUIRE( s1->enabledChannels(id1) == set );
    }

    SECTION("empty for invalid frame") {
        FrameID id1(0, 1);
        auto set = mux->enabledChannels(id1);
        REQUIRE( set.size() == 0 );
    }
}

TEST_CASE("Muxer onError", "[stream]") {
    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
    REQUIRE(s1);
    std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
    REQUIRE(s2);

    mux->add(s1);
    mux->add(s2);

    ftl::protocol::Error seenErr = ftl::protocol::Error::kNoError;
    auto h = mux->onError([&seenErr](ftl::protocol::Error err, const std::string &str) {
        seenErr = err;
        return true;
    });

    s1->fakeError(ftl::protocol::Error::kUnknown, "Unknown");

    REQUIRE( seenErr == ftl::protocol::Error::kUnknown );
}

TEST_CASE("Muxer mappings", "[stream]") {

    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    SECTION("can get local from remote") {
        std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
        REQUIRE(s1);
        std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        s1->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 0), Channel::kEndFrame);

        auto f1 = mux->findLocal(s2, FrameID(0, 0));

        REQUIRE( f1.frameset() == 1 );
    }

    SECTION("fails if mapping not valid") {
        std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
        REQUIRE(s1);
        std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        s1->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 0), Channel::kEndFrame);

        bool didThrow = false;

        try {
            mux->findLocal(s2, FrameID(1, 0));
        } catch(const ftl::exception &e) {
            e.what();
            didThrow = true;
        }

        REQUIRE( didThrow );
    }

    SECTION("can get remote from local") {
        std::shared_ptr<Stream> s1 = std::make_shared<TestStream>();
        REQUIRE(s1);
        std::shared_ptr<Stream> s2 = std::make_shared<TestStream>();
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        s1->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 0), Channel::kEndFrame);

        auto f1 = mux->findRemote(FrameID(1, 0));

        REQUIRE( f1.frameset() == 0 );
    }

    SECTION("can find stream by URI") {
        std::shared_ptr<Stream> s1 = std::make_shared<TestStream>("ftl://myuri1");
        REQUIRE(s1);
        std::shared_ptr<Stream> s2 = std::make_shared<TestStream>("ftl://myuri2");
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        auto foundS = mux->findStream("ftl://myuri2");
        REQUIRE( foundS == s2 );

        auto foundS2 = mux->findStream("ftl://myuri3");
        REQUIRE( foundS2 == nullptr );
    }
}

TEST_CASE("Muxer requests", "[stream]") {

    std::unique_ptr<Muxer> mux = std::make_unique<Muxer>();
    REQUIRE(mux);

    SECTION("can propagate specific request") {
        std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
        REQUIRE(s1);
        std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        s1->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 0), Channel::kEndFrame);

        std::atomic_int seenReq = 0;
        ftl::protocol::Request lastReq;

        auto h1 = mux->onRequest([&seenReq, &lastReq](const ftl::protocol::Request &req) {
            ++seenReq;
            lastReq = req;
            return true;
        });

        s2->sendRequest(Channel::kColour, 0, 0, 1);

        REQUIRE( seenReq == 1 );
        REQUIRE( lastReq.id.frameset() == 1 );
    }

    SECTION("can generate a single 255 frameset request") {
        std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
        REQUIRE(s1);
        std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        s1->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 0), Channel::kEndFrame);

        std::atomic_int seenReq = 0;
        ftl::protocol::Request lastReq;
        lastReq.id = 0;

        auto h1 = mux->onRequest([&seenReq, &lastReq](const ftl::protocol::Request &req) {
            ++seenReq;
            lastReq = req;
            return true;
        });

        s2->sendRequest(Channel::kColour, 255, 255, 1);

        REQUIRE( seenReq == 1 );
        REQUIRE( lastReq.id.frameset() == 1 );
        REQUIRE( mux->findRemote(lastReq.id).frameset() == 0 );
    }

    SECTION("can generate multiple requests from a 255 frameset") {
        std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
        REQUIRE(s1);
        std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        s1->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 1), Channel::kEndFrame);
        s2->seen(FrameID(1, 0), Channel::kEndFrame);

        std::atomic_int seenReq = 0;
        auto h1 = mux->onRequest([&seenReq](const ftl::protocol::Request &req) {
            ++seenReq;
            return true;
        });

        s2->sendRequest(Channel::kColour, 255, 255, 1);

        REQUIRE( seenReq == 3 );
    }

    SECTION("can generate multiple requests from a 255 frame") {
        std::shared_ptr<TestStream> s1 = std::make_shared<TestStream>();
        REQUIRE(s1);
        std::shared_ptr<TestStream> s2 = std::make_shared<TestStream>();
        REQUIRE(s2);

        mux->add(s1,1);
        mux->add(s2,1);

        s1->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 0), Channel::kEndFrame);
        s2->seen(FrameID(0, 1), Channel::kEndFrame);
        s2->seen(FrameID(1, 0), Channel::kEndFrame);

        std::atomic_int seenReq = 0;
        auto h1 = mux->onRequest([&seenReq](const ftl::protocol::Request &req) {
            ++seenReq;
            return true;
        });

        s2->sendRequest(Channel::kColour, 0, 255, 1);

        REQUIRE( seenReq == 2 );
    }
}
