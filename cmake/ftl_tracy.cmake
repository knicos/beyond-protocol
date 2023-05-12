if (ENABLE_PROFILER)
    set(TRACY_CXX_OPTIONS "-DTRACY_ENABLE -DTRACY_DELAYED_INIT -DTRACY_VERBOSE -DNOMINMAX")

    if (DEBUG_LOCKS)
        set(TRACY_CXX_OPTIONS "${TRACY_CXX_OPTIONS} -DDEBUG_LOCKS")
        message(STATUS "Lock profiling enabled")
    endif()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TRACY_CXX_OPTIONS}")

    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG 897aec5b062664d2485f4f9a213715d2e527e0ca # tags/v0.9.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(tracy)

    add_library(Tracy ALIAS TracyClient)

    message(STATUS "Profiling (Tracy) enabled")

else()
    add_library(Tracy INTERFACE)
endif()
