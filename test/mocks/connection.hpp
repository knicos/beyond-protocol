#pragma once

#include <map>
#include <string>
#include "../../src/peer.hpp"

ftl::net::PeerPtr createMockPeer(int c);

extern std::map<int, std::string> fakedata;

void send_handshake(ftl::net::Peer &p);

template <typename ARG>
msgpack::object packResponse(msgpack::zone &z, const ARG &arg) {
    return msgpack::object(arg, z);
}

void provideResponses(const ftl::net::PeerPtr &p, int c, const std::vector<std::tuple<bool,std::string,msgpack::object>> &responses);

template <typename T>
void writeNotification(int c, const std::string &name, const T &value) {
	auto res_obj = std::make_tuple(2,name,value);
	std::stringstream buf;
	msgpack::pack(buf, res_obj);
	fakedata[c] = buf.str();
}

template <typename T>
std::tuple<std::string, T> readResponse(int s) {
	msgpack::object_handle msg = msgpack::unpack(fakedata[s].data(), fakedata[s].size());
	std::tuple<uint8_t, std::string, T> req;
	msg.get().convert(req);
	return std::make_tuple(std::get<1>(req), std::get<2>(req));
}

template <typename T>
std::tuple<uint32_t, T> readRPC(int s) {
	msgpack::object_handle msg = msgpack::unpack(fakedata[s].data(), fakedata[s].size());
	std::tuple<uint8_t, uint32_t, std::string, T> req;
	msg.get().convert(req);
	return std::make_tuple(std::get<1>(req), std::get<3>(req));
}

template <typename T>
std::tuple<uint8_t, uint32_t, std::string, T>  readRPCFull(int s) {
	msgpack::object_handle msg = msgpack::unpack(fakedata[s].data(), fakedata[s].size());
	std::tuple<uint8_t, uint32_t, std::string, T> req;
	msg.get().convert(req);
	return req;
}

template <typename T>
std::tuple<uint8_t, std::string, T>  readNotifFull(int s) {
	msgpack::object_handle msg = msgpack::unpack(fakedata[s].data(), fakedata[s].size());
	std::tuple<uint8_t, std::string, T> req;
	msg.get().convert(req);
	return req;
}

template <typename T>
T readRPCReturn(int s) {
	msgpack::object_handle msg = msgpack::unpack(fakedata[s].data(), fakedata[s].size());
	std::tuple<uint8_t, uint32_t, msgpack::object, T> req;
	msg.get().convert(req);
	return std::get<3>(req);
}

