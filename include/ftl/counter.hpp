/**
 * @file counter.hpp
 * @copyright Copyright (c) 2022 University of Turku, MIT License
 * @author Nicolas Pope
 */

#pragma once

#include <atomic>

namespace ftl {

class Counter {
 public:
    inline explicit Counter(std::atomic_int *c): counter_(c) {
        ++(*c);
    }
    Counter() = delete;
    inline Counter(const Counter &c): counter_(c.counter_) {
        if (counter_) {
            ++(*counter_);
        }
    }
    inline Counter(Counter &&c): counter_(c.counter_) {
        c.counter_ = nullptr;
    }
    inline ~Counter() {
        if (counter_) {
            --(*counter_);
        }
    }

 private:
    std::atomic_int *counter_;
};

}  // namespace ftl
