#include "catch.hpp"
#include <ftl/protocol/channelUtils.hpp>

using ftl::protocol::Channel;
using std::string;

SCENARIO( "Channel names", "[utility]" ) {
    GIVEN( "a valid channel name" ) {
        REQUIRE( ftl::protocol::fromName("User") == Channel::kUser );
    }

    GIVEN( "an invalid channel name" ) {
        REQUIRE( ftl::protocol::fromName("RandomWord") == Channel::kNone );
    }

    GIVEN( "a channel, get a name" ) {
        REQUIRE( ftl::protocol::name(Channel::kUser) == "User" );
    }
}