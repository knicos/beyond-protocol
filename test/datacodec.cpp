#include "catch.hpp"
#include <ftl/codec/data.hpp>

SCENARIO( "Intrinsics pack/unpack" ) {
	GIVEN( "a valid instrincs object it packs" ) {
		ftl::data::Intrinsics intrin;
        std::get<0>(intrin).fx = 10.0f;

        std::vector<uint8_t> buffer;
        ftl::codec::pack(intrin, buffer);
        REQUIRE(buffer.size() > 0);

        auto result = ftl::codec::unpack<ftl::data::Intrinsics>(buffer);
        REQUIRE(std::get<0>(result).fx == 10.0f);
	}
}

SCENARIO( "Vector of strings pack/unpack" ) {
	GIVEN( "a valid instrincs object it packs" ) {
		std::vector<std::string> data = {"hello", "world"};

        std::vector<uint8_t> buffer;
        ftl::codec::pack(data, buffer);
        REQUIRE(buffer.size() > 0);

        auto result = ftl::codec::unpack<std::vector<std::string>>(buffer);
        REQUIRE(result[0] == "hello");
        REQUIRE(result[1] == "world");
	}
}
