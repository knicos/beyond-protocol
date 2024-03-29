/**
 * @file handle.hpp
 * @copyright Copyright (c) 2020 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <functional>
#include <unordered_map>
#include <utility>
#include <string>
#include <ftl/threads.hpp>
#include <ftl/exception.hpp>
#include <ftl/counter.hpp>

namespace ftl {

struct Handle;
struct BaseHandler {
    virtual ~BaseHandler() {}

    virtual void remove(const Handle &) = 0;

    virtual void removeUnsafe(const Handle &) = 0;

    inline Handle make_handle(BaseHandler*, int);

 protected:
    std::shared_mutex mutex_;
    int id_ = 0;
};

/**
 * A `Handle` is used to manage registered callbacks, allowing them to be
 * removed safely whenever the `Handle` instance is destroyed.
 */
struct [[nodiscard]] Handle {
    friend struct BaseHandler;

    /**
     * Cancel the callback and invalidate the handle.
     */
    inline void cancel() {
        if (handler_) {
            handler_->remove(*this);
        }
        handler_ = nullptr;
    }

    inline void innerCancel() {
        if (handler_) {
            handler_->removeUnsafe(*this);
        }
        handler_ = nullptr;
    }

    inline int id() const { return id_; }

    Handle() : handler_(nullptr), id_(0) {}

    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    inline Handle(Handle &&h) : handler_(nullptr) {
        if (handler_) handler_->remove(*this);
        handler_ = h.handler_;
        h.handler_ = nullptr;
        id_ = h.id_;
    }

    inline Handle &operator=(Handle &&h) {
        if (handler_) handler_->remove(*this);
        handler_ = h.handler_;
        h.handler_ = nullptr;
        id_ = h.id_;
        return *this;
    }

    inline ~Handle() {
        if (handler_) {
            handler_->remove(*this);
        }
    }

    private:
    BaseHandler *handler_;
    int id_;

    Handle(BaseHandler *h, int id) : handler_(h), id_(id) {}
};

/**
 * This class is used to manage callbacks. The template parameters are the
 * arguments to be passed to the callback when triggered. This class is already
 * thread-safe.
 *
 * POSSIBLE BUG:	On destruction any remaining handles will be left with
 * 					dangling pointer to Handler.
 */
template <typename ...ARGS>
struct Handler : BaseHandler {
    Handler() {}
    virtual ~Handler() {
        // Ensure all thread pool jobs are done
        while (jobs_ > 0 && ftl::pool.size() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    /**
     * Add a new callback function. It returns a `Handle` object that must
     * remain in scope, the destructor of the `Handle` will remove the callback.
     */
    Handle on(const std::function<bool(ARGS...)> &f) {
        std::unique_lock<std::shared_mutex> lk(mutex_);
        int id = id_++;
        callbacks_[id] = f;
        return make_handle(this, id);
    }

    /**
     * Safely trigger all callbacks. Note that `Handler` is locked when
     * triggering so callbacks cannot make modifications to it or they will
     * lock up. To remove a callback, return false from the callback, else
     * return true.
     */
    void trigger(ARGS ...args) {
        bool hadFault = false;
        std::string faultMsg;

        std::shared_lock<std::shared_mutex> lk(mutex_);
        for (auto i = callbacks_.begin(); i != callbacks_.end(); ) {
            bool keep = true;
            try {
                keep = i->second(args...);
            } catch(const std::exception &e) {
                hadFault = true;
                faultMsg = e.what();
            }
            if (!keep) {
                // i = callbacks_.erase(i);
                throw FTL_Error("Return value callback removal not implemented");
            } else {
                ++i;
            }
        }
        if (hadFault) throw FTL_Error("Callback exception: " << faultMsg);
    }

    /**
     * Call all the callbacks in another thread. The callbacks are done in a
     * single thread, not in parallel.
     */
    void triggerAsync(ARGS ...args) {
        ftl::pool.push([this, c = std::move(ftl::Counter(&jobs_)), args...](int id) {
            bool hadFault = false;
            std::string faultMsg;
            std::unique_lock<std::shared_mutex> lk(mutex_);
            for (auto i = callbacks_.begin(); i != callbacks_.end(); ) {
                bool keep = true;
                try {
                    keep = i->second(args...);
                } catch(const std::exception &e) {
                    hadFault = true;
                    faultMsg = e.what();
                }
                if (!keep) i = callbacks_.erase(i);
                else
                    ++i;
            }
            if (hadFault) throw FTL_Error("Callback exception: " << faultMsg);
        });
    }

    /**
     * Each callback is called in its own thread job. Note: the return value
     * of the callback is ignored in this case and does not allow callback
     * removal via the return value.
     */
    void triggerParallel(ARGS ...args) {
        std::unique_lock<std::shared_mutex> lk(mutex_);
        for (auto i = callbacks_.begin(); i != callbacks_.end(); ++i) {
            ftl::pool.push([this, c = std::move(ftl::Counter(&jobs_)), f = i->second, args...](int id) {
                try {
                    f(args...);
                } catch (const ftl::exception &e) {
                    throw e;
                }
            });
        }
    }

    /**
     * Remove a callback using its `Handle`. This is equivalent to allowing the
     * `Handle` to be destroyed or cancelled.
     */
    void remove(const Handle &h) override {
        {
            std::unique_lock<std::shared_mutex> lk(mutex_);
            callbacks_.erase(h.id());
        }
        // Make sure any possible call to removed callback has finished.
        while (jobs_ > 0 && ftl::pool.size() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    void removeUnsafe(const Handle &h) override {
        callbacks_.erase(h.id());
        // Make sure any possible call to removed callback has finished.
        while (jobs_ > 0 && ftl::pool.size() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    void clear() {
        callbacks_.clear();
    }

 private:
    std::unordered_map<int, std::function<bool(ARGS...)>> callbacks_;
    std::atomic_int jobs_ = 0;
};

/**
 * This class is used to manage callbacks. The template parameters are the
 * arguments to be passed to the callback when triggered. This class is already
 * thread-safe. Note that this version only allows a single callback at a time
 * and throws an exception if multiple are added without resetting.
 */
template <typename ...ARGS>
struct SingletonHandler : BaseHandler {
    /**
     * Add a new callback function. It returns a `Handle` object that must
     * remain in scope, the destructor of the `Handle` will remove the callback.
     */
    [[nodiscard]] Handle on(const std::function<bool(ARGS...)> &f) {
        std::unique_lock<std::shared_mutex> lk(mutex_);
        if (callback_) throw FTL_Error("Callback already bound");
        callback_ = f;
        return make_handle(this, id_++);
    }

    /**
     * Safely trigger all callbacks. Note that `Handler` is locked when
     * triggering so callbacks cannot make modifications to it or they will
     * lock up. To remove a callback, return false from the callback, else
     * return true.
     */
    bool trigger(ARGS ...args) {
        std::unique_lock<std::shared_mutex> lk(mutex_);
        if (callback_) {
            bool keep = callback_(std::forward<ARGS>(args)...);
            if (!keep) callback_ = nullptr;
            return keep;
        } else {
            return false;
        }
    }

    /**
     * Remove a callback using its `Handle`. This is equivalent to allowing the
     * `Handle` to be destroyed or cancelled. If the handle does not match the
     * currently bound callback then the callback is not removed.
     */
    void remove(const Handle &h) override {
        std::unique_lock<std::shared_mutex> lk(mutex_);
        if (h.id() == id_-1) callback_ = nullptr;
    }

    void removeUnsafe(const Handle &h) override {
        if (h.id() == id_-1) callback_ = nullptr;
    }

    void reset() { callback_ = nullptr; }

    operator bool() const { return static_cast<bool>(callback_); }

 private:
    std::function<bool(ARGS...)> callback_;
};

}  // namespace ftl

ftl::Handle ftl::BaseHandler::make_handle(BaseHandler *h, int id) {
    return ftl::Handle(h, id);
}
