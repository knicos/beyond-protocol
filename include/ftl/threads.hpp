/**
 * @file threads.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <mutex>
#include <shared_mutex>
#include <ftl/lib/ctpl_stl.hpp>

#define POOL_SIZE 10

/// consider using DECLARE_MUTEX(name) which allows (optional) profiling
#define MUTEX std::mutex
#define MUTEX_T MUTEX
/// consider using DECLARE_RECURSIVE_MUTEX(name) which allows (optional) profiling
#define RECURSIVE_MUTEX std::recursive_mutex
/// consider using DECLARE_SHARED_MUTEX(name) which allows (optional) profiling
#define SHARED_MUTEX std::shared_mutex

#if defined(TRACY_ENABLE)

#include <type_traits>
#include <tracy/Tracy.hpp>

// new macro
#define UNIQUE_LOCK_N(VARNAME, MUTEXNAME) std::unique_lock<LockableBase(MUTEX_T)> VARNAME(MUTEXNAME)

#define DECLARE_MUTEX(varname) TracyLockable(MUTEX, varname)
#define DECLARE_RECURSIVE_MUTEX(varname) TracyLockable(RECURSIVE_MUTEX, varname)
#define DECLARE_SHARED_MUTEX(varname) TracySharedLockable(SHARED_MUTEX, varname)

#define DECLARE_MUTEX_N(varname, name) TracyLockableN(MUTEX, varname, name)
#define DECLARE_RECURSIVE_MUTEX_N(varname, name) TracyLockableN(RECURSIVE_MUTEX, varname, name)
#define DECLARE_SHARED_MUTEX_N(varname, name) TracySharedLockableN(SHARED_MUTEX, varname, name)

/// mark lock acquired (mutex M);
#define MARK_LOCK_AQUIRED(M) LockMark(M)
// TODO: should automatic, but requires mutexes to be declared with DECLARE_..._MUTEX macros

#define UNIQUE_LOCK_T(M) std::unique_lock<std::remove_reference<decltype(M)>::type>
/// deprecated: use UNIQUE_LOCK_N instead
#define UNIQUE_LOCK(M, L) std::unique_lock<std::remove_reference<decltype(M)>::type> L(M)
/// deprecated: use SHARED_LOCK_N instead
#define SHARED_LOCK(M, L) std::shared_lock<std::remove_reference<decltype(M)>::type> L(M)

#else

#define UNIQUE_LOCK_N(VARNAME, MUTEXNAME) std::unique_lock<MUTEX_T> VARNAME(MUTEXNAME)

/// mutex with optional profiling (and debugging) when built with PROFILE_MUTEX.
#define DECLARE_MUTEX(varname) MUTEX varname
/// recursive mutex with optional profiling (and debugging) when built with PROFILE_MUTEX
#define DECLARE_RECURSIVE_MUTEX(varname) RECURSIVE_MUTEX varname
/// shared mutex with optional profiling (and debugging) when built with PROFILE_MUTEX
#define DECLARE_SHARED_MUTEX(varname) SHARED_MUTEX varname

#define DECLARE_MUTEX_N(varname, name) MUTEX varname
#define DECLARE_RECURSIVE_MUTEX_N(varname, name) RECURSIVE_MUTEX varname
#define DECLARE_SHARED_MUTEX_N(varname, name) SHARED_MUTEX varname

/// mark lock acquired (mutex M)
#define MARK_LOCK(M) {}

#define UNIQUE_LOCK_T(M) std::unique_lock<std::remove_reference<decltype(M)>::type>
/// deprecated: use UNIQUE_LOCK_N instead
#define UNIQUE_LOCK(M, L) std::unique_lock<std::remove_reference<decltype(M)>::type> L(M)
/// deprecated: use SHARED_LOCK_N instead
#define SHARED_LOCK(M, L) std::shared_lock<std::remove_reference<decltype(M)>::type> L(M)

#endif  // TRACY_ENABLE

#define SHARED_LOCK_TYPE(M) std::shared_lock<M>

namespace ftl {

extern ctpl::thread_pool pool;



/** FIFO task queue for thread pool. */
template<typename Task>
class TaskQueue {
public:
    void queue(Task task) {
        auto lk = std::unique_lock(mtx_);
        if (stop_) { return; }
        queue_.push_back(std::move(task));
        start_and_unlock(lk);
    }

    /** Try to queue new task*/
    bool try_queue(Task task, int max_queue_size) {
        auto lk = std::unique_lock(mtx_);
        if (stop_) { return false; }
        if (queue_.size() >= max_queue_size) { return false; }
        queue_.push_back(std::move(task));
        start_and_unlock(lk);
        return true;
    }

    /** Wait until finished. Returns if not stopped */
    void wait()
    {
        auto lk = std::unique_lock(mtx_);
        if (!stop_) { return; }
        if (queue_.size() == 0) { return; }
        cv_.wait_for(lk, [&](){ return queue_.size() == 0; });
    }

    void stop() {
        auto lk = std::unique_lock(mtx_);
        stop_ = true;
        if (!run_) {
            lk.unlock();
            cv_.notify_all();
        }
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Task> queue_;
    bool run_ = false;
    bool stop_ = false;

    void start_and_unlock(std::unique_lock<std::mutex>& lk) {
        if (!run_ && !stop_) {
            run_ = true;
            lk.unlock();
            pool.push(std::bind(&TaskQueue<Task>::run, this));
        }
    }

    void run() {
        while(true) {
            Task task;
            {
                auto lk = std::unique_lock(mtx_);
                if (!run_ || stop_ || (queue_.size() == 0)) {
                    run_ = false;
                    break;
                }

                task = queue_.front();
                queue_.pop_front();
            }

            task();
        }

        cv_.notify_all();
    }
};

namespace threads {

/** Upgrade shared lock to exclusive lock and re-acquire shared lock at end of 
 * scope. */
class _write_lock {
 public:
    explicit _write_lock(std::shared_mutex& mtx) : mtx_(&mtx) {
        mtx_->unlock_shared();
        mtx_->lock();
    }

    ~_write_lock() {
        mtx_->unlock();
        mtx_->lock_shared();
    }

 private:
    std::shared_mutex* const mtx_;
};

/** Upgrade already acquired SHARED_LOCK to exclusive lock and re-acquire shared
 * lock at end of scope. Shared lock must be already acquired on mutex! If more
 * careful locking is required, use std::..._lock directly */
#define WRITE_LOCK(M, L) ftl::threads::_write_lock L(M);

}  // namespace threads
}  // namespace ftl
