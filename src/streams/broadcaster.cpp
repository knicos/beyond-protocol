#include <ftl/protocol/broadcaster.hpp>

using ftl::protocol::Broadcast;
using ftl::protocol::StreamPacket;
using ftl::protocol::Packet;

Broadcast::Broadcast() {

}

Broadcast::~Broadcast() {

}

void Broadcast::add(const std::shared_ptr<Stream> &s) {
	UNIQUE_LOCK(mutex_,lk);

	streams_.push_back(s);
	handles_.push_back(std::move(s->onPacket([this,s](const StreamPacket &spkt, const Packet &pkt) {
		//LOG(INFO) << "BCAST Request: " << (int)spkt.streamID << " " << (int)spkt.channel << " " << spkt.timestamp;
		SHARED_LOCK(mutex_, lk);
		if (spkt.frameSetID() < 255) available(spkt.frameSetID()) += spkt.channel;
		cb_.trigger(spkt, pkt);
		if (spkt.streamID < 255) s->select(spkt.streamID, selected(spkt.streamID));
		return true;
	})));
}

void Broadcast::remove(const std::shared_ptr<Stream> &s) {
	UNIQUE_LOCK(mutex_,lk);
	// TODO: Find and remove handle also
	streams_.remove(s);
}

void Broadcast::clear() {
	UNIQUE_LOCK(mutex_,lk);
	handles_.clear();
	streams_.clear();
}

bool Broadcast::post(const StreamPacket &spkt, const Packet &pkt) {
	SHARED_LOCK(mutex_, lk);
	if (spkt.frameSetID() < 255) available(spkt.frameSetID()) += spkt.channel;

	bool status = true;
	for (auto s : streams_) {
		//s->select(spkt.frameSetID(), selected(spkt.frameSetID()));
		status = status && s->post(spkt, pkt);
	}
	return status;
}

bool Broadcast::begin() {
	bool r = true;
	for (auto &s : streams_) {
		r = r && s->begin();
	}
	return r;
}

bool Broadcast::end() {
	bool r = true;
	for (auto &s : streams_) {
		r = r && s->end();
	}
	return r;
}

bool Broadcast::active() {
	if (streams_.size() == 0) return false;
	bool r = true;
	for (auto &s : streams_) {
		r = r && s->active();
	}
	return r;
}

void Broadcast::reset() {
	for (auto &s : streams_) {
		s->reset();
	}
}
