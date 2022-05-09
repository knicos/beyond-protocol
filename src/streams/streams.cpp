#include <ftl/protocol/streams.hpp>

using ftl::protocol::Stream;
using ftl::protocol::Channel;
using ftl::protocol::ChannelSet;

const ChannelSet &Stream::available(int fs) const {
	SHARED_LOCK(mtx_, lk);
	if (fs < 0 || static_cast<uint32_t>(fs) >= state_.size()) throw FTL_Error("Frameset index out-of-bounds: " << fs);
	return state_[fs].available;
}

ChannelSet Stream::selected(int fs) const {
	SHARED_LOCK(mtx_, lk);
	if (fs < 0 || static_cast<uint32_t>(fs) >= state_.size()) throw FTL_Error("Frameset index out-of-bounds: " << fs);
	return state_[fs].selected;
}

ChannelSet Stream::selectedNoExcept(int fs) const {
	if (fs == 255) return {};

	SHARED_LOCK(mtx_, lk);
	if (fs < 0 || static_cast<uint32_t>(fs) >= state_.size()) return {};
	return state_[fs].selected;
}

void Stream::select(int fs, const ChannelSet &s, bool make) {
	if (fs == 255) return;

	UNIQUE_LOCK(mtx_, lk);
	if (fs < 0 || (!make && static_cast<uint32_t>(fs) >= state_.size())) throw FTL_Error("Frameset index out-of-bounds: " << fs);
	if (static_cast<uint32_t>(fs) >= state_.size()) state_.resize(fs+1);
	state_[fs].selected = s;
}

ChannelSet &Stream::available(int fs) {
	UNIQUE_LOCK(mtx_, lk);
	if (fs < 0) throw FTL_Error("Frameset index out-of-bounds: " << fs);
	if (static_cast<uint32_t>(fs) >= state_.size()) state_.resize(fs+1);
	return state_[fs].available;
}

void Stream::reset() {
	// Clear available and selected?
}
