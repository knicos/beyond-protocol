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

#include <ftl/net/common.hpp>
#include <ftl/net/peer.hpp>
#include <ftl/net/protocol.hpp>
#include <ftl/config.h>

#include "../src/socket.hpp"
#include "../src/protocol/connection.hpp"

#define _FTL_NET_UNIVERSE_HPP_

using std::tuple;
using std::get;
using std::vector;
using ftl::net::Peer;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;

// --- Mock --------------------------------------------------------------------

namespace ftl {
namespace net {

typedef unsigned int callback_t;

class Universe {
	public:
	Universe() {};

	ftl::UUID id() { return ftl::UUID(); }

	void _notifyConnect(Peer*) {}
	void _notifyDisconnect(Peer*) {}
	void removeCallback(callback_t id) {}

	callback_t onConnect(const std::function<void(Peer*)> &f) { return 0; }
	callback_t onDisconnect(const std::function<void(Peer*)> &f) { return 0; }

	size_t getSendBufferSize(ftl::URI::scheme_t s) const { return 10*1024; }
	size_t getRecvBufferSize(ftl::URI::scheme_t s) const { return 10*1024; }
};
}
}

using ftl::net::internal::Socket;
using ftl::net::internal::SocketConnection;

// Mock connection, reads/writes from fakedata
// TODO: use separate in/out data
static std::map<int, std::string> fakedata;
//static std::mutex fakedata_mtx;

class Connection_Mock : public SocketConnection {
public:
	const int id_;
	Connection_Mock(int id) : SocketConnection(), id_(id) {

	}

	void connect(const ftl::URI&, int) override {}

	ssize_t send(const char* buffer, size_t len) override {
		fakedata[id_] += std::string(buffer, len);
		return len;
	}
	
	ssize_t recv(char *buffer, size_t len) override {
		if (fakedata.count(id_) == 0) {
			// this is an error in test
			std::cout << "unrecognised socket, test error (FIXME)" << std::endl;
			return 0;
		}

		size_t l = fakedata[id_].size();
		CHECK(l <= len); // FIXME: buffer overflow
		std::memcpy(buffer, fakedata[id_].c_str(), l);
		
		fakedata.erase(id_);

		return l;
	}

	ssize_t writev(const struct iovec *iov, int iovcnt) override {
		size_t sent = 0;
		std::stringstream ss;
		for (int i = 0; i < iovcnt; i++) {
			ss << std::string((char*)(iov[i].iov_base), size_t(iov[i].iov_len));
			sent += iov[i].iov_len;
		}
		fakedata[id_] += ss.str();
		return sent;
	}
};

static std::atomic<int> ctr_ = 0;
std::unique_ptr<SocketConnection> ftl::net::internal::createConnection(const ftl::URI &uri) {
	return std::make_unique<Connection_Mock>(ctr_++);
}

bool ftl::net::internal::Socket::is_fatal() { 
	return false;
}

class MockPeer : public Peer {
private:
	MockPeer(int) :
		Peer(ftl::net::internal::createConnection(ftl::URI("")), new ftl::net::Universe()),
		idx(ctr_ - 1) {}
	
	MockPeer(std::string uri) :
		Peer(ftl::URI(""), new ftl::net::Universe()), idx(ctr_ - 1) {}

public:
	int idx;
	
	void mock_data() { data(); }

	static MockPeer create_connecting_peer() {
		return MockPeer("");
	};
	
	static MockPeer create_listening_peer() {
		return MockPeer(0);
	};
};

// --- Support -----------------------------------------------------------------

void send_handshake(Peer &p) {
	ftl::UUID id;
	p.send("__handshake__", ftl::net::kMagic, ((8 << 16) + (5 << 8) + 2), id);
}

template <typename T>
tuple<std::string, T> readResponse(int s) {
	msgpack::object_handle msg = msgpack::unpack(fakedata[s].data(), fakedata[s].size());
	tuple<uint8_t, std::string, T> req;
	msg.get().convert(req);
	return std::make_tuple(get<1>(req), get<2>(req));
}

template <typename T>
tuple<uint32_t, T> readRPC(int s) {
	msgpack::object_handle msg = msgpack::unpack(fakedata[s].data(), fakedata[s].size());
	tuple<uint8_t, uint32_t, std::string, T> req;
	msg.get().convert(req);
	return std::make_tuple(get<1>(req), get<3>(req));
}

template <typename T>
T readRPCReturn(int s) {
	msgpack::object_handle msg = msgpack::unpack(fakedata[s].data(), fakedata[s].size());
	tuple<uint8_t, uint32_t, std::string, T> req;
	msg.get().convert(req);
	return get<3>(req);
}

// --- Files to test -----------------------------------------------------------

#include "../src/peer.cpp"

// --- Tests -------------------------------------------------------------------

TEST_CASE("Peer(int)", "[]") {
	SECTION("initiates a valid handshake") {
		MockPeer s = MockPeer::create_listening_peer();

		auto [name, hs] = readResponse<ftl::net::Handshake>(0);
		
		REQUIRE( name == "__handshake__" );
		
		// 1) Sends magic (64bits)
		REQUIRE( get<0>(hs) == ftl::net::kMagic );
		 
		// 2) Sends FTL Version
		REQUIRE( get<1>(hs) == (FTL_VERSION_MAJOR << 16) + (FTL_VERSION_MINOR << 8) + FTL_VERSION_PATCH );
		
		// 3) Sends peer UUID
		
		
		REQUIRE( s.status() == Peer::kConnecting );
	}
	
	SECTION("completes on full handshake") {
		MockPeer s_l = MockPeer::create_listening_peer();
		MockPeer s_c = MockPeer::create_connecting_peer();
		
		// get sent message by s_l and place it in s_c's buffer
		fakedata[s_c.idx] = fakedata[s_l.idx]; 
		s_l.mock_data(); // listenin peer: process
		// vice versa, listening peer gets reply and processes it
		fakedata[s_l.idx] = fakedata[s_c.idx]; 
		s_c.mock_data(); // connecting peer: process
		sleep_for(milliseconds(50));

		// both peers should be connected now
		REQUIRE( s_c.status() == Peer::kConnected );
		REQUIRE( s_l.status() == Peer::kConnected );
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
}

TEST_CASE("Peer::call()", "[rpc]") {
	MockPeer s = MockPeer::create_connecting_peer();
	send_handshake(s);
	s.mock_data();
	sleep_for(milliseconds(50));
	
	SECTION("one argument call") {
		REQUIRE( s.isConnected() );
		
		fakedata[s.idx] = "";
		
		// Thread to provide response to otherwise blocking call
		std::thread thr([&s]() {
			while (fakedata[s.idx].size() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
			
			auto [id,value] = readRPC<tuple<int>>(s.idx);
			auto res_obj = std::make_tuple(1,id,"__return__",get<0>(value)+22);
			std::stringstream buf;
			msgpack::pack(buf, res_obj);
			fakedata[s.idx] = buf.str();
			s.mock_data();
			sleep_for(milliseconds(50));
		});
		int res = s.call<int>("test1", 44);
		thr.join();
		
		REQUIRE( (res == 66) );
	}
	
	SECTION("no argument call") {
		REQUIRE( s.isConnected() );
		
		fakedata[s.idx] = "";
		
		// Thread to provide response to otherwise blocking call
		std::thread thr([&s]() {
			while (fakedata[s.idx].size() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
			
			auto res = readRPC<tuple<>>(s.idx);
			auto res_obj = std::make_tuple(1,std::get<0>(res),"__return__",77);
			std::stringstream buf;
			msgpack::pack(buf, res_obj);
			fakedata[s.idx] = buf.str();
			s.mock_data();
			sleep_for(milliseconds(50));
		});
		
		int res = s.call<int>("test1");
		
		thr.join();
		
		REQUIRE( (res == 77) );
	}

	SECTION("vector return from call") {
		REQUIRE( s.isConnected() );
		
		fakedata[s.idx] = "";
		
		// Thread to provide response to otherwise blocking call
		std::thread thr([&s]() {
			while (fakedata[s.idx].size() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
			
			auto res = readRPC<tuple<>>(s.idx);
			vector<int> data = {44,55,66};
			auto res_obj = std::make_tuple(1,std::get<0>(res),"__return__",data);
			std::stringstream buf;
			msgpack::pack(buf, res_obj);
			fakedata[s.idx] = buf.str();
			s.mock_data();
			sleep_for(milliseconds(50));
		});
		
		vector<int> res = s.call<vector<int>>("test1");
		
		thr.join();
		
		REQUIRE( (res[0] == 44) );
		REQUIRE( (res[2] == 66) );
	}
}

TEST_CASE("Peer::bind()", "[rpc]") {
	MockPeer s = MockPeer::create_listening_peer();
	send_handshake(s);
	s.mock_data();
	sleep_for(milliseconds(50));
	

	SECTION("no argument call") {
		bool done = false;
		
		s.bind("hello", [&]() {
			done = true;
		});

		s.send("hello");
		s.mock_data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( done );
	}
	
	SECTION("one argument call") {		
		int done = 0;
		
		s.bind("hello", [&](int a) {
			done = a;
		});
		
		s.send("hello", 55);
		s.mock_data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( (done == 55) );
	}
	
	SECTION("two argument call") {		
		std::string done;
		
		s.bind("hello", [&](int a, std::string b) {
			done = b;
		});

		s.send("hello", 55, "world");
		s.mock_data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( (done == "world") );
	}

	SECTION("int return value") {		
		int done = 0;
		
		s.bind("hello", [&](int a) -> int {
			done = a;
			return a;
		});
		
		s.asyncCall<int>("hello", [](int a){}, 55);
		s.mock_data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( (done == 55) );
		REQUIRE( (readRPCReturn<int>(s.idx) == 55) );
	}

	SECTION("vector return value") {		
		int done = 0;
		
		s.bind("hello", [&](int a) -> vector<int> {
			done = a;
			vector<int> b = {a,45};
			return b;
		});
		
		s.asyncCall<int>("hello", [](int a){}, 55);
		s.mock_data(); // Force it to read the fake send...
		sleep_for(milliseconds(50));
		
		REQUIRE( (done == 55) );

		auto res = readRPCReturn<vector<int>>(s.idx);
		REQUIRE( (res[1] == 45) );
	}
}

TEST_CASE("Socket::send()", "[io]") {
	MockPeer s = MockPeer::create_connecting_peer();
	sleep_for(milliseconds(50));

	SECTION("send an int") {
		int i = 607;
		
		s.send("dummy",i);
		
		auto [name, value] = readResponse<tuple<int>>(s.idx);
		
		REQUIRE( (name == "dummy") );
		REQUIRE( (get<0>(value) == 607) );
	}
	
	SECTION("send a string") {
		std::string str("hello world");
		s.send("dummy",str);
		
		auto [name, value] = readResponse<tuple<std::string>>(s.idx);
		
		REQUIRE( (name == "dummy") );
		REQUIRE( (get<0>(value) == "hello world") );
	}
	
	SECTION("send const char* string") {
		s.send("dummy","hello world");
		
		auto [name, value] = readResponse<tuple<std::string>>(s.idx);
		
		REQUIRE( (name == "dummy") );
		REQUIRE( (get<0>(value) == "hello world") );
	}
	
	SECTION("send a tuple") {
		auto tup = std::make_tuple(55,66,true,6.7);
		s.send("dummy",tup);
		
		auto [name, value] = readResponse<tuple<decltype(tup)>>(s.idx);
		
		REQUIRE( (name == "dummy") );
		REQUIRE( (get<1>(get<0>(value)) == 66) );
	}
	
	SECTION("send multiple strings") {
		std::string str("hello ");
		std::string str2("world");
		s.send("dummy2",str,str2);
		
		auto [name, value] = readResponse<tuple<std::string,std::string>>(s.idx);
		
		REQUIRE( (name == "dummy2") );
		REQUIRE( (get<0>(value) == "hello ") );
		REQUIRE( (get<1>(value) == "world") );
	}
}

