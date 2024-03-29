#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/node.hpp>
#include <ftl/uri.hpp>
#include <ftl/exception.hpp>

#include <thread>
#include <chrono>

using std::this_thread::sleep_for;
using std::chrono::milliseconds;

// --- Support -----------------------------------------------------------------

static bool try_for(int count, const std::function<bool()> &f) {
    int i=count;
    while (i-- > 0) {
        if (f()) return true;
        sleep_for(milliseconds(10));
    }
    return false;
}

// --- Tests -------------------------------------------------------------------

TEST_CASE("Garbage bug", "[net]") {
    auto self = ftl::createDummySelf();
    
    self->listen(ftl::URI("tcp://localhost:0")); 

    auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
    LOG(INFO) << uri;
    auto p = ftl::connectNode(uri);
    REQUIRE( p );
    
    REQUIRE( p->waitConnection(5) );
    
    REQUIRE( self->waitConnections(5) == 1 );
    REQUIRE( ftl::getSelf()->numberOfNodes() == 1);

    p.reset();
    ftl::protocol::reset();
}

TEST_CASE("Listen and Connect", "[net]") {
    auto self = ftl::createDummySelf();
    
    self->listen(ftl::URI("tcp://localhost:0")); 

    SECTION("valid tcp connection using ipv4") {
        auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
        LOG(INFO) << uri;
        auto p = ftl::connectNode(uri);
        REQUIRE( p );
        
        REQUIRE( p->waitConnection(5) );
        
        REQUIRE( self->waitConnections(5) == 1 );
        REQUIRE( ftl::getSelf()->numberOfNodes() == 1);
    }

    SECTION("valid tcp connection using hostname") {
        auto uri = "tcp://localhost:" + std::to_string(self->getListeningURIs().front().getPort());
        auto p = ftl::connectNode(uri);
        REQUIRE( p );
        
        REQUIRE( p->waitConnection(5) );
        
        REQUIRE( self->waitConnections(5) == 1 );
        REQUIRE( ftl::getSelf()->numberOfNodes() == 1);
    }

    SECTION("invalid protocol") {
        bool throws = false;
        try {
            auto p = ftl::connectNode("http://localhost:1234");
        }
        catch (const ftl::exception& ex) {
            ex.ignore();
            throws = true;
        }
        REQUIRE(throws);
    }

    /* not sure the rest of the code handles reconnets correctly anyways

    SECTION("automatic reconnect from originating connection") {
        auto uri = "tcp://localhost:" + std::to_string(self->getListeningURIs().front().getPort());

        auto p_connecting = ftl::connectNode(uri);
        REQUIRE(p_connecting);

        REQUIRE(p_connecting->waitConnection(5));
        p_connecting->close(true);

        REQUIRE(p_connecting->status() != ftl::protocol::NodeStatus::kConnected);
        REQUIRE(p_connecting->waitConnection(5));
    }

    SECTION("automatic reconnect from remote termination") {
        auto uri = "tcp://localhost:" + std::to_string(self->getListeningURIs().front().getPort());

        auto p_connecting = ftl::connectNode(uri);
        REQUIRE(p_connecting);

        REQUIRE(p_connecting->waitConnection(5));
        REQUIRE(p_connecting->connectionCount() == 1);
        
        auto nodes = self->getNodes();
        REQUIRE( nodes.size() == 1 );
        for (auto &node : nodes) {
            node->waitConnection(5);
            node->close();
        }

        bool r = try_for(500, [p_connecting]{ return p_connecting->connectionCount() >= 2; });
        REQUIRE( r );
    }*/

    ftl::protocol::reset();
}

TEST_CASE("Self::onConnect()", "[net]") {
    auto self = ftl::createDummySelf();
    
    self->listen(ftl::URI("tcp://localhost:0")); 

    auto uri = "tcp://localhost:" + std::to_string(self->getListeningURIs().front().getPort());

    SECTION("single valid remote init connection") {
        bool done = false;

        auto h = self->onConnect([&](const std::shared_ptr<ftl::protocol::Node> &p_listening) {
            done = true;
            return true;
        });

        REQUIRE( ftl::connectNode(uri) );

        bool result = try_for(20, [&done]{ return done; });
        REQUIRE( result );
    }

    SECTION("single valid init connection") {
        bool done = false;

        auto h = ftl::getSelf()->onConnect([&](const std::shared_ptr<ftl::protocol::Node> &p_listening) {
            done = true;
            return true;
        });

        REQUIRE( ftl::connectNode(uri)->waitConnection(5) );

        REQUIRE( done );
    }

    ftl::protocol::reset();
}

/*TEST_CASE("Universe::onDisconnect()", "[net]") {
    Universe a;
    Universe b;

    a.listen(ftl::URI("tcp://localhost:0"));
    auto uri = "tcp://localhost:" + std::to_string(a.getListeningURIs().front().getPort());

    SECTION("single valid remote close") {
        bool done = false;

        a.onDisconnect([&done](Peer *p) {
            done = true;
        });

        Peer *p = b.connect(uri);
        p->waitConnection();
        sleep_for(milliseconds(20));
        p->close();

        REQUIRE( try_for(20, [&done]{ return done; }) );
    }

    SECTION("single valid close") {
        bool done = false;

        b.onDisconnect([&done](Peer *p) {
            done = true;
        });

        Peer *p = b.connect(uri);
        p->waitConnection();
        sleep_for(milliseconds(20));
        p->close();

        REQUIRE( try_for(20, [&done]{ return done; }) );
    }
}

TEST_CASE("Universe::broadcast()", "[net]") {
    Universe a;
    Universe b;
    
    a.listen(ftl::URI("tcp://localhost:0"));
    auto uri = "tcp://localhost:" + std::to_string(a.getListeningURIs().front().getPort());
    
    SECTION("no arguments to no peers") {
        bool done = false;
        a.bind("hello", [&done]() {
            done = true;
        });
        
        b.broadcast("done");
        
        sleep_for(milliseconds(50));
        REQUIRE( !done );
    }
    
    SECTION("no arguments to one peer") {
        b.connect(uri)->waitConnection();
        
        bool done = false;
        a.bind("hello", [&done]() {
            done = true;
        });
        
        b.broadcast("hello");
        
        REQUIRE( try_for(20, [&done]{ return done; }) );
    }
    
    SECTION("one argument to one peer") {
        b.connect(uri)->waitConnection();
        
        int done = 0;
        a.bind("hello", [&done](int v) {
            done = v;
        });
        
        b.broadcast("hello", 676);
        
        REQUIRE( try_for(20, [&done]{ return done == 676; }) );
    }
    
    SECTION("one argument to two peers") {
        Universe c;
        
        b.connect(uri)->waitConnection();
        c.connect(uri)->waitConnection();
        
        int done1 = 0;
        b.bind("hello", [&done1](int v) {
            done1 = v;
        });
        
        int done2 = 0;
        c.bind("hello", [&done2](int v) {
            done2 = v;
        });

        REQUIRE( a.numberOfPeers() == 2 );
        //sleep_for(milliseconds(100)); // NOTE: Binding might not be ready
        
        a.broadcast("hello", 676);
        
        REQUIRE( try_for(20, [&done1, &done2]{ return done1 == 676 && done2 == 676; }) );
    }
}

TEST_CASE("Universe::findAll()", "") {
    Universe a;
    Universe b;
    Universe c;

    a.listen(ftl::URI("tcp://localhost:0"));
    auto uri = "tcp://localhost:" + std::to_string(a.getListeningURIs().front().getPort());

    b.connect(uri)->waitConnection();
    c.connect(uri)->waitConnection();

    SECTION("no values exist") {
        REQUIRE( (c.findAll<int>("test_all").size() == 0) );
    }

    SECTION("one set exists") {
        a.bind("test_all", []() -> std::vector<int> {
            return {3,4,5};
        });

        auto res = c.findAll<int>("test_all");
        REQUIRE( (res.size() == 3) );
        REQUIRE( (res[0] == 3) );
    }

    SECTION("two sets exists") {
        b.bind("test_all", []() -> std::vector<int> {
            return {3,4,5};
        });
        c.bind("test_all", []() -> std::vector<int> {
            return {6,7,8};
        });

        //sleep_for(milliseconds(100)); // NOTE: Binding might not be ready

        auto res = a.findAll<int>("test_all");
        REQUIRE( (res.size() == 6) );
        REQUIRE( (res[0] == 3 || res[0] == 6) );
    }
}

TEST_CASE("Peer::call() __ping__", "") {
    Universe a;
    Universe b;
    Universe c;

    a.listen(ftl::URI("tcp://localhost:0"));
    auto uri = "tcp://localhost:" + std::to_string(a.getListeningURIs().front().getPort());

    auto *p = b.connect(uri);
    p->waitConnection();

    SECTION("single ping") {
        int64_t res = p->call<int64_t>("__ping__");
        REQUIRE((res <= ftl::timer::get_time() && res > 0));
    }

    SECTION("large number of pings") {
        for (int i=0; i<100; ++i) {
            int64_t res = p->call<int64_t>("__ping__");
            REQUIRE(res > 0);
        }
    }

    SECTION("large number of parallel pings") {
        std::atomic<int> count = 0;
        for (int i=0; i<100; ++i) {
            ftl::pool.push([&count, p](int id) {
                int64_t res = p->call<int64_t>("__ping__");
                REQUIRE( res > 0 );
                count++;
            });
        }

        while (count < 100) std::this_thread::sleep_for(milliseconds(5));
    }

    SECTION("single invalid rpc") {
        bool errored = false;
        try {
            int64_t res = p->call<int64_t>("__ping2__");
            REQUIRE( res > 0 );  // Not called or required actually
        } catch (const ftl::exception &e) {
            e.ignore(); // supress log output
            errored = true;
        }

        REQUIRE(errored);
    }
}*/
