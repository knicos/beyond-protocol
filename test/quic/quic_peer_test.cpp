#include "../catch.hpp"

#include "../src/universe.hpp"
#include "../src/peer.hpp"

#include "quic_universe.hpp"
#include "quic_peer.hpp"

#include <thread>

TEST_CASE("QUIC Universe/Peer")
{
    auto net1 = std::make_unique<ftl::net::Universe>();
    auto net2 = std::make_unique<ftl::net::Universe>();

    net1->listen(ftl::URI("quic://0.0.0.0:9001"));
    net2->connect("quic://127.0.0.1:9001/");

    {
        std::promise<bool> promise;
        auto handle = net1->onConnect([&](const ftl::net::PeerPtr& Peer){
            promise.set_value(true);
            return false;
        });
        auto future = promise.get_future();
        REQUIRE(future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
    }

    REQUIRE(net2->getPeers().size() == 1);

    auto p1 = net1->getPeers().front();
    auto p2 = net2->getPeers().front();

    {
        std::promise<int> promise;
        std::vector<char> data = {1, 2, 3, 4, 5, 6, 7, 8 ,9};

        p1->bind("__test__", [&](std::vector<char> data) {
            promise.set_value(1);
        });

        p2->send("__test__", data);

        auto future = promise.get_future();
        future.wait();
        
        CHECK(future.get() == 1);
    }
}
