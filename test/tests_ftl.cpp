#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include <ftl/threads.hpp>

/** Stop thread pool before main() returns. */
int main(int argc, char** argv) {
	try {
		auto retval = Catch::Session().run(argc, argv);
		// Windows: thread pool must be stopped before main()
		//          returns, otherwise thread::join() may deadlock.
		ftl::pool.stop();
		return retval;
	}
	catch (const std::exception& ex) {
		return -1;
	}
}