#pragma once

namespace  ftl
{

/** Clock synchronization between two peers.
 * 
 * Description:
 *  1. Call updateInfo(): records the time of call
 *     and calls get_time on remote.
 *  2. Remote replies with current time. 
 *  3. Local calculates rtt using time from step 1 and
 *     replies with time from 2, current time from local,
 *     and the calculated rtt.
 *  4. Remote calculates rtt using value from reply (originally
 *     from 2 ). Now both local and remote have an estimate
 *     of rtt and clock offset.
 * 
 * RTT and offset estimates are the means from the 2nd and 3rd quantiles.
 * Total number of samples used can be with constructor parameter.
 * 
 * updateInfo() should be called periodically to update the estimates.
 * 
 * TODO: Clock synchronization should happen on separate (UDP) channel
 *       to avoid the impact of retransmissions and delays by other messages
 *       in the queue.
 */
class ClockInfo {
public:
    ClockInfo(class ftl::net::PeerBase*, int n=50);

    /** Estimated offset between remote and local clock (ms) */
    int64_t getClockOffset() const;

    /** Round trip time between peers (ms) */
    int64_t getRtt() const;

    int64_t getJitter() const;

    /** Update clock sync. Synchronous. */
    void updateInfo();

    /** Run updateInfo in loop to get initial estimate of rtt and offset.
     *  Returns true on success.
     */
    bool init();

private:
    class ftl::net::PeerBase* const remote_;

    void update_(int64_t offset, int32_t rtt);
    void update_time_sync_(int64_t t_request_local, int64_t t_reply_remote, int32_t rtt_remote);
    void update_estimate_();

    const int n_samples_;
    int n_;

    struct SyncEvent {
        int64_t offset;
        int32_t rtt;
    };
    std::vector<SyncEvent> history_;

    int64_t rtt_;
    int64_t rtt_fastest_;
    int64_t rtt_slowest_;
    int64_t offset_;
};

} // namespace  ftl
