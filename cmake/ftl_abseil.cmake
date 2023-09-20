set(ABSL_PROPAGATE_CXX_STD ON)
FetchContent_Declare(
	abseil
	GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
	GIT_TAG        c2435f8342c2d0ed8101cb43adfd605fdc52dca2 # 20230125.3
)
FetchContent_MakeAvailable(abseil)
