#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/node.hpp>
#include <ftl/protocol/streams.hpp>
#include <ftl/uri.hpp>
#include <nlohmann/json.hpp>

// --- Tests -------------------------------------------------------------------

TEST_CASE("Webservice connection", "[net]") {
    ftl::getSelf()->onNodeDetails([]() -> nlohmann::json {
        return {
            {"id", ftl::protocol::id.to_string()}
        };
    });

	SECTION("connect using secure websocket") {
		std::string uri;
        if(const char* env_p = std::getenv("FTL_WEBSERVICE_URI")) {
            uri = std::string(env_p);
        } else {
            return;
        }

		auto p = ftl::connectNode(uri);
		REQUIRE( p );
		
		REQUIRE( p->waitConnection(5) );

        auto details = p->details();
        REQUIRE(details.contains("id"));

        LOG(INFO) << "Details: " << details.dump();
	}

    /*SECTION("can create a net stream") {
		std::string uri;
        if(const char* env_p = std::getenv("FTL_WEBSERVICE_URI")) {
            uri = std::string(env_p);
        } else {
            return;
        }

		auto p = ftl::connectNode(uri);
		REQUIRE( p );
		
		REQUIRE( p->waitConnection(5) );

        auto details = p->details();
        REQUIRE(details.contains("id"));

        ftl::protocol::Request req;
        req.id = 0;

        auto stream = ftl::createStream("ftl://ftlab.utu.fi/teststream");
        auto h = stream->onRequest([&req](const ftl::protocol::Request &r) {
            req = r;
            return true;
        });

        stream->begin();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        REQUIRE( req.id.frameset() == 255 );
	}*/

	ftl::protocol::reset();
}
