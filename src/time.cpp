#include <ftl/time.hpp>
#include <chrono>

using std::chrono::time_point_cast;
using std::chrono::milliseconds;
using std::chrono::high_resolution_clock;

static int64_t clock_adjust = 0;

int64_t ftl::time::get_time() {
	return time_point_cast<milliseconds>(high_resolution_clock::now()).time_since_epoch().count()+clock_adjust;
}

int64_t ftl::time::get_time_micro() {
	return time_point_cast<std::chrono::microseconds>(high_resolution_clock::now()).time_since_epoch().count()+(clock_adjust*1000);
}

double ftl::time::get_time_seconds() {
	return time_point_cast<std::chrono::microseconds>(high_resolution_clock::now()).time_since_epoch().count() / 1000000.0 + (static_cast<double>(clock_adjust) / 1000.0);
}

void ftl::time::setClockAdjustment(int64_t ms) {
	clock_adjust += ms;
}
