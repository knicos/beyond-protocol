#pragma once

#include <cinttypes>

namespace ftl {
namespace time {

/**
 * Get current (monotonic) time in milliseconds.
 */
int64_t get_time();

int64_t get_time_micro();
double get_time_seconds();

/**
 * Add the specified number of milliseconds to the clock when generating
 * timestamps. This is used to synchronise clocks on multiple machines as it
 * influences the behaviour of the timer.
 */
void setClockAdjustment(int64_t ms);

}
}
