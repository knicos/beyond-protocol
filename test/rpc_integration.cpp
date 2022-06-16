#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/node.hpp>
#include <ftl/protocol/service.hpp>
#include <ftl/uri.hpp>
#include <ftl/exception.hpp>
#include <ftl/lib/nlohmann/json.hpp>
#include <ftl/protocol/streams.hpp>

#include <thread>
#include <chrono>

using std::this_thread::sleep_for;
using std::chrono::milliseconds;

// --- Tests -------------------------------------------------------------------

TEST_CASE("RPC Node Details", "[rpc]") {
    auto self = ftl::createDummySelf();

    self->listen(ftl::URI("tcp://localhost:0"));

    {
        auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
        LOG(INFO) << uri;
        auto p = ftl::connectNode(uri);
        REQUIRE(p);
        REQUIRE(p->waitConnection(5));
        REQUIRE(self->waitConnections(5) == 1);

        self->onNodeDetails([]() {
            nlohmann::json details;
            details["title"] = "Test node";
            return details;
        });

        auto details = p->details();

        REQUIRE(details["title"] == "Test node");
    }

    ftl::protocol::reset();
}

TEST_CASE("RPC List Streams", "[rpc]") {
    auto self = ftl::createDummySelf();

    self->listen(ftl::URI("tcp://localhost:0"));

    {
        auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
        LOG(INFO) << uri;
        auto p = ftl::setServiceProvider(uri);
        REQUIRE(p);
        REQUIRE(p->waitConnection(5));
        REQUIRE(self->waitConnections(5) == 1);

        auto s = self->createStream("ftl://mystream");
        s->begin();

        auto streams = ftl::getSelf()->getStreams();

        REQUIRE(streams.size() == 1);
        REQUIRE(streams[0] == "ftl://mystream");

        s->end();

        streams = ftl::getSelf()->getStreams();
        REQUIRE(streams.size() == 0);
    }

    ftl::protocol::reset();
}

TEST_CASE("RPC get config", "[rpc]") {
    auto self = ftl::createDummySelf();

    self->listen(ftl::URI("tcp://localhost:0"));

    {
        auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
        LOG(INFO) << uri;
        auto p = ftl::connectNode(uri);
        REQUIRE(p);
        REQUIRE(p->waitConnection(5));
        REQUIRE(self->waitConnections(5) == 1);

        self->onGetConfig([](const std::string &path) {
            nlohmann::json cfg;
            cfg["path"] = path;
            return cfg;
        });

        auto cfg = p->getConfig("path is this");

        REQUIRE(cfg["path"] == "path is this");
    }

    ftl::protocol::reset();
}

TEST_CASE("RPC shutdown", "[rpc]") {
    auto self = ftl::createDummySelf();

    self->listen(ftl::URI("tcp://localhost:0"));

    {
        auto uri = "tcp://127.0.0.1:" + std::to_string(self->getListeningURIs().front().getPort());
        LOG(INFO) << uri;
        auto p = ftl::connectNode(uri);
        REQUIRE(p);
        REQUIRE(p->waitConnection(5));
        REQUIRE(self->waitConnections(5) == 1);

        bool shutdown = false;
        self->onShutdown([&shutdown]() {
            shutdown = true;
        });

        ftl::getSelf()->shutdownAll();
        sleep_for(milliseconds(100));
        REQUIRE(shutdown);
    }

    ftl::protocol::reset();
}
