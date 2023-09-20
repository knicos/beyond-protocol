#include "catch.hpp"

#include <nlohmann/json.hpp>

#include "../src/universe.hpp"
#include "../src/peer.hpp"
#include "../src/protocol.hpp"

#include <thread>
#include <chrono>

#include "../src/protocol/connection.hpp"
#include "../src/protocol/tcp.hpp"

// msgpack uses binary format when DTYPE is char
typedef char DTYPE;
constexpr int BSIZE = (1 << 23); // bytes in each send (2^20 = 1 MiB)
constexpr int COUNT = 100; // how many times send called

using namespace ftl::net;

static std::vector<DTYPE> data_test;
static std::atomic_uint64_t recv_cnt_ = 0;
static auto t_last_recv_ = std::chrono::steady_clock::now();

static void recv_data(const std::vector<DTYPE> &data) {
	recv_cnt_.fetch_add(data.size() * sizeof(DTYPE));
	t_last_recv_ = std::chrono::steady_clock::now();
}

static float peer_send(ftl::net::PeerBase* p, const std::vector<DTYPE>& data, int cnt) {
	auto t_start = std::chrono::steady_clock::now();
	decltype(t_start) t_stop;

	size_t bytes_sent = 0;
	size_t bytes = data.size() * sizeof(DTYPE);

	for (int i = 0; i < cnt; i++) {
		p->send("recv_data", data);
		bytes_sent += bytes;
	}

	t_stop = std::chrono::steady_clock::now();

	// should be ok, since blocking sockets are used
	float ms = std::chrono::duration_cast<std::chrono::milliseconds>
					(t_stop - t_start).count();
	float throughput_send =  (float(bytes_sent >> 20)/ms)*1000.0f*8.0f;

	LOG(INFO) << "sent " << (bytes_sent >> 20) << " MiB in " << ms << " ms, "
			  << "connection throughput: "
			  << throughput_send << " MBit/s";

	ms = std::chrono::duration_cast<std::chrono::milliseconds>
					(t_last_recv_ - t_start).count();
	float throughput_recv =  (float(recv_cnt_ >> 20)/ms)*1000.0f*8.0f;

	LOG(INFO) << "received " << (bytes_sent >> 20) << " MiB in " << ms << " ms, "
			  << "connection throughput: "
			  << throughput_recv << " MBit/s";

	recv_cnt_ = 0;

	return (throughput_send + throughput_recv)/2.0f;
}

ftl::URI uri("");

/* 
 * About 10800 MBit/s (i5-9600K), with ASAN 
 */

TEST_CASE("throughput", "[net]") {
	auto net_server = std::make_unique<Universe>();
	net_server->setLocalID(ftl::UUID());
	auto net_client = std::make_unique<Universe>();
	net_client->setLocalID(ftl::UUID());

	net_server->bind("test_server", [](){ LOG(INFO) << "test_server"; });
	net_client->bind("test_client", [](){ LOG(INFO) << "test_client"; });
	net_server->bind("recv_data", recv_data);

	data_test.clear(); data_test.reserve(BSIZE);
	for (int i = 0; i < BSIZE; i++) { data_test.push_back(i ^ (i - 1)); }

	std::string host = "localhost";
	int port = 0; // pick random port
	net_server->listen(ftl::URI("tcp://" + host + ":" + std::to_string(port)));
	int listening_port = net_server->getListeningURIs()[0].getPort();
	uri = ftl::URI("tcp://localhost:" + std::to_string(listening_port));

	SECTION("TCP throughput") {
		LOG(INFO) << "connecting to " << uri.to_string();
		auto p = net_client->connect(uri);

		while(!p->isConnected()) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }

		auto r = peer_send(p.get(), data_test, COUNT);
		REQUIRE(r > 1000);
	}
}
