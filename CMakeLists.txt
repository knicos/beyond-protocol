cmake_minimum_required (VERSION 3.18.0)
include (CheckIncludeFile)
include (CheckIncludeFileCXX)
include (CheckFunctionExists)

if (NOT FTL_VERSION)
	set(FTL_VERSION 0.0.1)
endif()

project (beyond-protocol VERSION "${FTL_VERSION}")

include(GNUInstallDirs)
include(CTest)

enable_testing()

option(WITH_GNUTLS "Enable TLS support" ON)
option(USE_CPPCHECK "Apply cppcheck during build" ON)
option(BUILD_TESTS "Compile all unit and integration tests" ON)
option(BUILD_EXAMPLES "Compile the examples" ON)

if (NOT WIN32)
	option(WITH_PYTHON "Enable python support" ON)
endif()

if (BUILD_TESTS)
	include_directories(src/lib/catch)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Threads REQUIRED)
find_package(URIParser REQUIRED)
find_package(MsgPack REQUIRED)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
	set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
	message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
endif()

# ==== GnuTLS ==================================================================

if (WITH_GNUTLS)
	#find_package(GnuTLS REQUIRED)
	include(FindGnuTLS)
	message(STATUS "Gnutls found: ${GNUTLS_FOUND}")
	set(HAVE_GNUTLS true)
else()
	add_library(gnutls INTERFACE)
endif()

# ==============================================================================

if(${CMAKE_VERSION} VERSION_GREATER "3.12.0")
	cmake_policy(SET CMP0074 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)

# ==============================================================================

# Incase find_package failed, manually look for msgpack
if (NOT MsgPack_FOUND)
	check_include_file_cxx("msgpack.hpp" MSGPACK_FOUND)
	if (NOT MSGPACK_FOUND)
		message(ERROR "Msgpack is required")
	endif()
else()
	if(WIN32)
		# Find include
		find_path(MSGPACK_INCLUDE_DIRS
			NAMES msgpack.hpp
			PATHS "C:/Program Files/msgpack" "C:/Program Files (x86)/msgpack"
			PATH_SUFFIXES include
		)
	endif()
	# TODO(nick) Create imported target.
	include_directories(${MSGPACK_INCLUDE_DIRS})
endif()

# Only need libuuid on Linux, Windows has its own
if (NOT WIN32)
check_include_file("uuid/uuid.h" UUID_FOUND)
if (NOT UUID_FOUND)
	message(ERROR "UUID library is required")
endif()
find_library(UUID_LIBRARIES NAMES uuid libuuid)
else()
endif()

# Optional source problem check
if (USE_CPPCHECK)
	find_program(CPPCHECK_FOUND cppcheck)
	if (CPPCHECK_FOUND)
		message(STATUS "Found cppcheck: will perform source checks")
		set(CMAKE_CXX_CPPCHECK "cppcheck" "-D__align__(A)" "--enable=warning,performance,style" "--inline-suppr" "--std=c++17" "--suppress=*:*loguru.*" "--suppress=*:*catch.hpp" "--suppress=*:*elas*" "--suppress=*:*json.hpp" "--quiet")
	endif()
endif()

include(git_version)
include(ftl_paths)

if (WIN32) # TODO(nick) Should do based upon compiler (VS)
	add_definitions(-DWIN32)
	#set(CMAKE_GENERATOR_TOOLSET "host=x64")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2 /MP /std:c++17 /wd4996 /Zc:__cplusplus")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /DFTL_DEBUG /Wall")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Wall")
	set(OS_LIBS "")
else()
	add_definitions(-DUNIX)
	# -fdiagnostics-color
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -std=c++17 -fPIC -march=haswell -mavx2 -mfpmath=sse -Wall -Werror")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -pg")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3")
	set(OS_LIBS "dl")
endif()

SET(CMAKE_USE_RELATIVE_PATHS ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ==============================================================================

#if (WITH_PYTHON)
#	find_package(Python COMPONENTS Interpreter Development REQUIRED)
#	if (Python_FOUND)
#		add_subdirectory(src/ext/pybind11)
#		set(HAVE_PYTHON TRUE)
#		message(STATUS "Python module enabled")
#	else()
#		message(ERROR "Python dependencies not found, Python module is not built")
#	endif()
#endif()

### Generate Build Configuration Files =========================================

configure_file(${CMAKE_SOURCE_DIR}/include/ftl/protocol/config.h.in
			   ${CMAKE_SOURCE_DIR}/include/ftl/protocol/config.h
)

configure_file(${CMAKE_SOURCE_DIR}/src/config.cpp.in
			   ${CMAKE_SOURCE_DIR}/src/config.cpp
)

if (WIN32) # TODO(nick) Should do based upon compiler (VS)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${VS_STARTUP_PROJECT})
	#set_property(TARGET voltu-node PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${VS_DEBUG_WORKING_DIRECTORY})
endif()

include(ftl_CPack)

### ========

include_directories("include/ftl/lib")

add_library(beyond-common OBJECT
	src/ctpl_stl.cpp
	src/dispatcher.cpp
	src/exception.cpp
	src/loguru.cpp
	src/uri.cpp
	src/config.cpp
	src/time.cpp
	src/base64.cpp
	src/channelSet.cpp
)

target_include_directories(beyond-common PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>)

add_library(beyond-protocol STATIC
	$<TARGET_OBJECTS:beyond-common>
    src/peer.cpp
    src/universe.cpp
	src/socket/socket.cpp
	src/protocol/connection.cpp
	src/protocol/factory.cpp
	src/protocol/tcp.cpp
	src/protocol/tls.cpp
	src/protocol/websocket.cpp
	src/streams/streams.cpp
	src/streams/muxer.cpp
	src/streams/broadcaster.cpp
	src/streams/netstream.cpp
	src/streams/filestream.cpp
	src/node.cpp
	src/self.cpp
	src/protocol.cpp
	src/rpc.cpp
    src/channelUtils.cpp
    src/service.cpp
    src/codecs/golomb.cpp
    src/codecs/h264.cpp
)

target_include_directories(beyond-protocol PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>)
target_link_libraries(beyond-protocol Threads::Threads ${UUID_LIBRARIES} GnuTLS::GnuTLS)

install(TARGETS beyond-protocol
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (BUILD_TESTS)
	add_subdirectory(test)
endif()

if (BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
