#include "catch.hpp"
#include <ftl/protocol.hpp>
#include <ftl/protocol/self.hpp>
#include <ftl/protocol/node.hpp>
#include <ftl/uri.hpp>
#include <nlohmann/json.hpp>

// --- Tests -------------------------------------------------------------------

TEST_CASE("Webservice connection", "[net]") {
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

	ftl::protocol::reset();
}
