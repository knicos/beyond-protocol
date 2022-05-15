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

// #define DEBUG_MUTEX
#define MUTEX_TIMEOUT 2

#if defined DEBUG_MUTEX
#include <ftl/lib/loguru.hpp>
#include <chrono>
#include <type_traits>

#define MUTEX std::timed_mutex
#define RECURSIVE_MUTEX std::recursive_timed_mutex
#define SHARED_MUTEX std::shared_timed_mutex

#define UNIQUE_LOCK(M, L) std::unique_lock<std::remove_reference<decltype(M)>::type> L(M, std::chrono::milliseconds(MUTEX_TIMEOUT)); while (!L) { LOG(ERROR) << "Mutex timeout"; L.try_lock_for(std::chrono::milliseconds(MUTEX_TIMEOUT)); };
#define SHARED_LOCK(M, L) std::shared_lock<std::remove_reference<decltype(M)>::type> L(M, std::chrono::milliseconds(MUTEX_TIMEOUT)); while (!L) { LOG(ERROR) << "Mutex timeout"; L.try_lock_for(std::chrono::milliseconds(MUTEX_TIMEOUT)); };

#else
#define MUTEX std::mutex
#define RECURSIVE_MUTEX std::recursive_mutex
#define SHARED_MUTEX std::shared_mutex

#define UNIQUE_LOCK(M, L) std::unique_lock<std::remove_reference<decltype(M)>::type> L(M);
#define SHARED_LOCK(M, L) std::shared_lock<std::remove_reference<decltype(M)>::type> L(M);
#endif  // DEBUG_MUTEX

#define SHARED_LOCK_TYPE(M) std::shared_lock<M>

namespace ftl {
    extern ctpl::thread_pool pool;

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
