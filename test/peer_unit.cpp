/** Peer unit test. Does not test lower level protocols (TLS/WebSocket) */

#include "catch.hpp"

#include <iostream>
#include <memory>

#include <vector>
#include <tuple>
#include <thread>
#include <chrono>
#include <functional>
#include <sstream>

#include <peer.hpp>
#include <protocol.hpp>
#include <ftl/protocol.hpp>
#include <ftl/protocol/error.hpp>
#include <ftl/protocol/config.h>
#include <ftl/handle.hpp>

#include "mocks/connection.hpp"

using std::tuple;
using std::get;
using std::vector;
using ftl::net::Peer;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;
using ftl::protocol::NodeStatus;

static std::atomic<int> ctr_ = 0;

// --- Tests -------------------------------------------------------------------

TEST_CASE("Peer(int)", "[]") {
	SECTION("initiates a valid handshake") {
		auto s = createMockPeer(ctr_++);
		s->start();

		LOG(INFO) << "STARTED";

		auto [name, hs] = readResponse<ftl::net::Handshake>(0);
		
		REQUIRE( name == "__handshake__" );
		
		// 1) Sends magic (64bits)
		REQUIRE( get<0>(hs) == ftl::net::kMagic );
		 
		// 2) Sends FTL Version
		REQUIRE( get<1>(hs) == (FTL_VERSION_MAJOR << 16) + (FTL_VERSION_MINOR << 8) + FTL_VERSION_PATCH );
		
		// 3) Sends peer UUID
		
		
		REQUIRE( s->status() == NodeStatus::kConnecting );
	}
	
	SECTION("completes on full handshake") {
		int lidx = ctr_++;
		int cidx = ctr_++;
		auto s_l = createMockPeer(lidx);
		auto s_c = createMockPeer(cidx);

		s_l->start();
		
		// get sent message by s_l and place it in s_c's buffer
		fakedata[cidx] = fakedata[lidx]; 
		s_l->data(); // listenin peer: process
		// vice versa, listening peer gets reply and processes it
		fakedata[lidx] = fakedata[cidx]; 
		s_c->data(); // connecting peer: process
		sleep_for(milliseconds(50));

		// both peers should be connected now
		REQUIRE( s_c->status() == NodeStatus::kConnected );
		REQUIRE( s_l->status() == NodeStatus::kConnected );
	}
	
	SECTION("has correct version on full handshake") {
		
		// MockPeer s = MockPeer::create_connecting_peer();
		// Send handshake response
		// send_handshake(s);
		// s.mock_data();
		//sleep_for(milliseconds(50));
		// REQUIRE( (s.getFTLVersion() ==  (8 << 16) + (5 << 8) + 2) );
	}
	
	SECTION("has correct peer id on full handshake") {
		// MockPeer s = MockPeer::create_connecting_peer();
		//
		// Send handshake response
		//REQUIRE( s.id() ==   );
	}

	ftl::protocol::reset();
}

TEST_CASE("Peer::call()", "[rpc]") {
	int c = ctr_++;
	auto s = createMockPeer(c);
	send_handshake(*s.get());
	s->data();
	sleep_for(milliseconds(50));
	
	SECTION("one argument call") {
		REQUIRE( s->isConnected() );
		
		fakedata[c] = "";
		
		// Thread to provide response to otherwise blocking call
		std::thread thr([&s, c]() {
			while (fakedata[c].size() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
			
			auto [id,value] = readRPC<tuple<int>>(c);
			auto res_obj = std::make_tuple(1,id,"__return__",get<0>(value)+22);
			std::stringstream buf;
			msgpack::pack(buf, res_obj);
			fakedata[c] = buf.str();
			s->data();
			sleep_for(milliseconds(50));
		});
		int res = s->call<int>("test1", 44);
		thr.join();
		
		REQUIRE( (res == 66) );
	}
	
	SECTION("no argument call") {
		REQUIRE( s->isConnected() );
		
		fakedata[c] = "";
		
		// Thread to provide response to otherwise blocking call
		std::thread thr([&s, c]() {
			while (fakedata[c].size() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
			
			auto res = readRPC<tuple<>>(c);
			auto res_obj = std::make_tuple(1,std::get<0>(res),"__return__",77);
			std::stringstream buf;
			msgpack::pack(buf, res_obj);
			fakedata[c] = buf.str();
			s->data();
			sleep_for(milliseconds(50));
		});
		
		int res = s->call<int>("test1");
		
		thr.join();
		
		REQUIRE( (res == 77) );
	}

	SECTION("vector return from call") {
		REQUIRE( s->isConnected() );
		
		fakedata[c] = "";
		
		// Thread to provide response to otherwise blocking call
		std::thread thr([&s, c]() {
			while (fakedata[c].size() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
			
			auto res = readRPC<tuple<>>(c);
			vector<int> data = {44,55,66};
			auto res_obj = std::make_tuple(1,std::get<0>(res),"__return__",data);
			std::stringstream buf;
			msgpack::pack(buf, res_obj);
			fakedata[c] = buf.str();
			s->data();
			sleep_for(milliseconds(50));
		});
		
		vector<int> res = s->call<vector<int>>("test1");
		
		thr.join();
		
		REQUIRE( (res[0] == 44) );
		REQUIRE( (res[2] == 66) );
	}

	s.reset();
	ftl::protocol::reset();
}

TEST_CASE("Peer::bind()", "[rpc]") {
	int c = ctr_++;
	auto s = createMockPeer(c);
	send_handshake(*s.get());
	s->data();
	sleep_for(milliseconds(50));
	

	SECTION("no argument call") {
		bool done = false;
		
		s->bind("hello", [&]() {
			done = true;
		});

		s->send("hello");
		s->data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( done );
	}
	
	SECTION("one argument call") {		
		int done = 0;
		
		s->bind("hello", [&](int a) {
			done = a;
		});
		
		s->send("hello", 55);
		s->data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( (done == 55) );
	}
	
	SECTION("two argument call") {		
		std::string done;
		
		s->bind("hello", [&](int a, const std::string &b) {
			done = b;
		});

		s->send("hello", 55, "world");
		s->data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( (done == "world") );
	}

	SECTION("int return value") {		
		int done = 0;
		
		s->bind("hello", [&](int a) -> int {
			done = a;
			return a;
		});
		
		s->asyncCall<int>("hello", [](int a){}, 55);
		s->data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( (done == 55) );
		REQUIRE( (readRPCReturn<int>(c) == 55) );
	}

	SECTION("vector return value") {		
		int done = 0;
		
		s->bind("hello", [&](int a) -> vector<int> {
			done = a;
			vector<int> b = {a,45};
			return b;
		});
		
		s->asyncCall<int>("hello", [](int a){}, 55);
		s->data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( (done == 55) );

		auto res = readRPCReturn<vector<int>>(c);
		REQUIRE( (res[1] == 45) );
	}

	s.reset();
	ftl::protocol::reset();
}

TEST_CASE("Socket::send()", "[io]") {
	int c = ctr_++;
	auto s = createMockPeer(c);
	sleep_for(milliseconds(50));

	SECTION("send an int") {
		int i = 607;
		
		s->send("dummy",i);
		
		auto [name, value] = readResponse<tuple<int>>(c);
		
		REQUIRE( (name == "dummy") );
		REQUIRE( (get<0>(value) == 607) );
	}
	
	SECTION("send a string") {
		std::string str("hello world");
		s->send("dummy",str);
		
		auto [name, value] = readResponse<tuple<std::string>>(c);
		
		REQUIRE( (name == "dummy") );
		REQUIRE( (get<0>(value) == "hello world") );
	}
	
	SECTION("send const char* string") {
		s->send("dummy","hello world");
		
		auto [name, value] = readResponse<tuple<std::string>>(c);
		
		REQUIRE( (name == "dummy") );
		REQUIRE( (get<0>(value) == "hello world") );
	}
	
	SECTION("send a tuple") {
		auto tup = std::make_tuple(55,66,true,6.7);
		s->send("dummy",tup);
		
		auto [name, value] = readResponse<tuple<decltype(tup)>>(c);
		
		REQUIRE( (name == "dummy") );
		REQUIRE( (get<1>(get<0>(value)) == 66) );
	}
	
	SECTION("send multiple strings") {
		std::string str("hello ");
		std::string str2("world");
		s->send("dummy2",str,str2);
		
		auto [name, value] = readResponse<tuple<std::string,std::string>>(c);
		
		REQUIRE( (name == "dummy2") );
		REQUIRE( (get<0>(value) == "hello ") );
		REQUIRE( (get<1>(value) == "world") );
	}

	s.reset();
	ftl::protocol::reset();
}

