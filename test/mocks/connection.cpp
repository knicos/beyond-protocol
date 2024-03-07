#include "connection.hpp"

#include "../../src/socket.hpp"
#include "../../src/universe.hpp"
#include "../../src/protocol/connection.hpp"
#include "../../src/uuidMSGPACK.hpp"
#include "../../src/protocol.hpp"
#include <ftl/protocol/self.hpp>
#include <chrono>

using ftl::net::internal::Socket;
using ftl::net::internal::SocketConnection;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;

// Mock connection, reads/writes from fakedata
// TODO: use separate in/out data
std::map<int, std::string> fakedata;

class Connection_Mock : public SocketConnection {
public:
	const int id_;
	bool valid_ = true;
	explicit Connection_Mock(int id) : SocketConnection(), id_(id) {

	}

	void connect(const ftl::URI&, int) override {}

	bool is_valid() override { return valid_; }

	bool close() override { valid_ = false; return true; }

	SOCKET fd() override { return -1; }

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

	bool set_recv_buffer_size(size_t sz) override { return true; }
	bool set_send_buffer_size(size_t sz) override { return true; }
	size_t get_recv_buffer_size() override { return 1024; }
	size_t get_send_buffer_size() override { return 1024; }
};

ftl::net::PeerTcpPtr createMockPeer(int c) {
	ftl::net::Universe *u = ftl::getSelf()->getUniverse();
	std::unique_ptr<ftl::net::internal::SocketConnection> conn = std::make_unique<Connection_Mock>(c);
	
	return u->injectFakePeer(std::move(conn));
}

void mockRecv(ftl::net::PeerTcpPtr& peer) {
    peer->recv();
	while (peer->jobs() > 0) sleep_for(milliseconds(1));
}

void send_handshake(ftl::net::PeerTcp &p) {
	ftl::UUID id;
	p.send("__handshake__", (uint64_t) ftl::net::kMagic, (uint64_t) ((8 << 16) + (5 << 8) + 2), ftl::UUIDMSGPACK(id));
}

void provideResponses(const ftl::net::PeerPtr &p_base, int c, const std::vector<std::tuple<bool,std::string,msgpack::object>> &responses) {
	auto p = std::dynamic_pointer_cast<ftl::net::PeerTcp>(p_base);
	if (!p) { LOG(FATAL) << "Peer ptr not of type PeerTcp"; return; }

    for (const auto &response : responses) {
        auto [notif,expname,resdata] = response;
        while (fakedata[c].size() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::string name;
        int id;
        if (notif) {
            auto [t,n,v] = readNotifFull<msgpack::object>(c);
            name = n;
            id = -1;
        } else {
            auto [t,i,n,v] = readRPCFull<msgpack::object>(c);
            name = n;
            id = i;
        }

        if (name != expname) return;
        if (!notif) {
            auto res_obj = std::make_tuple(1,id,msgpack::object(), resdata);
            std::stringstream buf;
            msgpack::pack(buf, res_obj);
            fakedata[c] = buf.str();
            p->recv();
            sleep_for(milliseconds(50));
        } else {
            fakedata[c] = "";
        }
    }
}
