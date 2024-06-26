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

#if defined(TRACY_ENABLE) && defined(DEBUG_LOCKS)

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
#define UNIQUE_LOCK_MUTEX_T std::unique_lock<LockableBase(MUTEX_T)>

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
#define UNIQUE_LOCK_MUTEX_T std::unique_lock<MUTEX_T>

/// deprecated: use UNIQUE_LOCK_N instead
#define UNIQUE_LOCK(M, L) std::unique_lock<std::remove_reference<decltype(M)>::type> L(M)
/// deprecated: use SHARED_LOCK_N instead
#define SHARED_LOCK(M, L) std::shared_lock<std::remove_reference<decltype(M)>::type> L(M)

#endif  // TRACY_ENABLE

#define SHARED_LOCK_TYPE(M) std::shared_lock<M>

namespace ftl {

void set_thread_name(const std::string& name);

extern ctpl::thread_pool pool;

namespace threads {

class Batch {
private:
    int busy_ = 0;
    std::mutex mtx_;
    std::condition_variable cv_;

    template<typename T>
    void add_lk_(T func) {
        pool.push([this, func=std::move(func)](int){
            try {
                func();
            }
            catch (const std::exception& ex) {}

            int count = 0;
            {
                auto lk = std::unique_lock(mtx_);
                count = --busy_;
                if (count == 0) { cv_.notify_all(); }
            }
        });
        busy_++;
    }

public:
    Batch() {};
    ~Batch() {
        wait();
    };

    template<typename T>
    inline void add(std::initializer_list<T> funcs) {
        auto lk = std::unique_lock(mtx_);
        for (const auto& func : funcs) {
            add_lk_(std::move(func));
        }
    }

    template<typename T>
    inline void add(T func) {
        auto lk = std::unique_lock(mtx_);
        add_lk_(std::move(func));
    }

    inline void wait() {
        auto lk = std::unique_lock(mtx_);
        if (busy_ == 0) { return; } 
        cv_.wait(lk, [&](){ return busy_ == 0; });
    };

    template<typename T>
    inline bool wait_for(T t) {
        auto lk = std::unique_lock(mtx_);
        if (busy_ == 0) { return true; } 
        return cv_.wait_for(lk, t, [&](){ return busy_ == 0; });
    };

    template<typename T>
    inline bool wait_until(T t) {
        auto lk = std::unique_lock(mtx_);
        if (busy_ == 0) { return true; } 
        return cv_.wait_until(lk, t, [&](){ return busy_ == 0; });
    };
};

}

template<auto Func, typename... Args>
class WorkerQueue {
public:
    /** Add task to the queue. Returns number of tasks in queue before the new task. 
     *  If queue does not accept new tasks, returns -1 (stopped). */
    int queue(Args... args) {
        auto lk = std::unique_lock(mtx_);
        if (stop_) { return -1; }
        int size = queue_.size();
        queue_.push_back(std::make_tuple(args...));
        start_and_unlock(lk);
        return size;
    }

    /** Try to queue new task if queue is not larger than max_queue_size. 
     *  Returns true if task was queued. */
    bool try_queue(int max_queue_size, Args... args) {
        auto lk = std::unique_lock(mtx_);
        if (stop_) { return false; }
        if (queue_.size() >= max_queue_size) { return false; }
        queue_.push_back(std::make_tuple(args...));
        start_and_unlock(lk);
        return true;
    }

    /** Queue, but discards front of the queue until at max_size if queue over max_size. Returns queue size before insert or discards. */
    int queue_discard(int max_size, Args... args) {
        auto lk = std::unique_lock(mtx_);
        if (stop_) { return false; }
        int queue_size = queue_.size();
        while (queue_.size() >= max_size) { queue_.pop_front(); }
        queue_.push_back(std::make_tuple(args...));
        start_and_unlock(lk);
        return queue_size;
    };

    /** Wait for any running tasks. Returns immediately if not stopped. */
    void wait() {
        auto lk = std::unique_lock(mtx_);
        if (!stop_) { return; }
        if (!busy_) { return; }
        cv_.wait(lk, [&](){ return !busy_; });
    }

    /** Stop any further processing. Call wait() to wait for any running tasks to complete. */
    void stop(bool clear_queue=false) {
        auto lk = std::unique_lock(mtx_);
        stop_ = true;
        if (clear_queue) { queue_.clear(); }
        if (!busy_) {
            lk.unlock();
            cv_.notify_all();
        }
    }

    /** Remove all queued tasks */
    void clear() {
        auto lk = std::unique_lock(mtx_);
        queue_.clear();
    }

    /** Continue processing remaining queue and accept new tasks to the queue. */
    void resume() {
        auto lk = std::unique_lock(mtx_);
        stop_ = false;
        if ((queue_.size() > 0) && !busy_) {
            start_and_unlock(lk);
        }
    }

    ~WorkerQueue() {
        stop(true);
        wait();
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::tuple<Args...>> queue_;
    bool busy_ = false;
    bool stop_ = false;

    void start_and_unlock(std::unique_lock<std::mutex>& lk) {
        if (!busy_ && !stop_) {
            busy_ = true;
            lk.unlock();
            pool.push(std::bind(&WorkerQueue<Func, Args...>::run, this));
        } else { lk.unlock(); }
    }

    void run() {
        while(true) {
            std::tuple<Args...> args;
            {
                auto lk = std::unique_lock(mtx_);
                if (stop_ || (queue_.size() == 0)) {
                    busy_ = false;
                    cv_.notify_all();
                    break;
                }

                args = std::move(queue_.front());
                queue_.pop_front();
            }
            try {
                std::apply<decltype(Func)>(Func, args);
            }
            catch (const std::exception& ex) {
                //LOG(ERROR) << "Task failed with exception: " << ex.what();
            }
        }
    }

protected:
};

struct TaskQueueBase {
    static void apply(const std::function<void()>& f) { f(); }
};

class TaskQueue : public WorkerQueue<TaskQueueBase::apply, typename std::function<void()>> {
public:

};

namespace threads {

}  // namespace threads
}  // namespace ftl
