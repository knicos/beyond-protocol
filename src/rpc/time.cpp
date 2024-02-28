#include "../peer.hpp"
#include "time.hpp"

#include <chrono>

#include <ftl/time.hpp>

#include <loguru.hpp>

using ftl::ClockInfo;

int64_t ClockInfo::getClockOffset() const { return offset_; }
int64_t ClockInfo::getRtt() const { return rtt_; }

int64_t ClockInfo::getJitter() const { return rtt_slowest_ - rtt_fastest_; }

ClockInfo::ClockInfo(ftl::net::PeerBase* peer, int n) : remote_(peer), n_samples_(n), n_(0) {
    history_.resize(n_samples_);

    // FIXME: get_time bind fails silently (Why?). Does not appear to be  used elsewhere.
    remote_->bind("__get_time__", ftl::time::get_time);
    remote_->bind("__update_time_sync__", 
        [this](int64_t t_request_local, int64_t t_reply_remote, int32_t rtt_remote) {
            update_time_sync_(t_request_local, t_reply_remote, rtt_remote);
    });
}

void ClockInfo::updateInfo() {
    auto t_start = ftl::time::get_time();
    // FIXME: this must be asynchronous
    int64_t t_remote = remote_->call<int64_t>("__get_time__");
    auto t_end = ftl::time::get_time();
    
    auto rtt = t_end - t_start;
    auto t_offset = t_remote - t_start - rtt/2; 

    remote_->send("__update_time_sync__", t_remote, t_end, rtt);
    
    update_(t_offset, rtt);
}

bool ClockInfo::init() {
    int fails = 0;
    for (int i = 0; i < n_samples_; i++) {
        try { updateInfo(); }
        catch (const std::exception& ex) {
            // Try again at most n_samples_ times in total.
            if (fails++ < n_samples_) { i--; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return fails < n_samples_;
}

void ClockInfo::update_time_sync_(int64_t t_request_local, int64_t t_reply_remote, int32_t rtt_remote) {
    auto rtt_local = ftl::time::get_time() - t_request_local;
    auto t_offset = t_reply_remote - t_request_local - rtt_local/2; 

    update_(t_offset, rtt_local);
}

void ClockInfo::update_(int64_t offset, int32_t rtt) {
    int pos = n_++;
    n_ = (n_ >= n_samples_) ? 0 : n_;
    history_[pos] = { offset, rtt };
    update_estimate_();
}

void ClockInfo::update_estimate_() {
    auto sorted = history_;
    // Clock drift (offsets) to be small compared to rtt, so sorting by rtt should be enough
    std::sort(sorted.begin(), sorted.end(),
        [](auto& a, auto& b) { return a.rtt < b.rtt; });
    
    // Exclude first and last quartile and calculate mean values from remaining half
    int begin = sorted.size()/4;
    int count = sorted.size() - begin*2;
    int64_t offset = 0;
    int64_t rtt = 0;
    for (int i = 0; i < count; i++) {
        offset += sorted[begin + i].offset;
        rtt    += sorted[begin + i].rtt;
    }
    offset_ = offset/count;
    rtt_ = rtt/count;
    rtt_fastest_ = sorted.front().rtt;
    rtt_slowest_ = sorted.back().rtt;
    
    //LOG(INFO) << "[final] offset: " << offset_ << ", rtt: " << rtt_ << ", latency: " << rtt_/2;
}
