if (ENABLE_PROFILER)
    set(TRACY_CXX_OPTIONS "-DTRACY_ENABLE -DNOMINMAX")

    if (DEBUG_LOCKS)
        # This doesn't seem to do anything. Log debugging/profiling should be done per-lock basis, otherwise
        # profiler will quickly run out of memory. 
        set(TRACY_CXX_OPTIONS "${TRACY_CXX_OPTIONS} -DDEBUG_LOCKS")
        message(STATUS "Lock profiling enabled")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TRACY_CXX_OPTIONS}")

    set(TRACY_ON_DEMAND OFF) # Problems in Unreal if ON
    set(TRACY_DELAYED_INIT ON)

    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG 897aec5b062664d2485f4f9a213715d2e527e0ca # tags/v0.9.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(tracy)

    set_property(TARGET TracyClient PROPERTY POSITION_INDEPENDENT_CODE ON)

    message(STATUS "Profiling (Tracy) enabled")

else()
    add_library(TracyClient INTERFACE)
endif()
