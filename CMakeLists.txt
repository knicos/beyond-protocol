cmake_minimum_required (VERSION 3.18.0)
include (CheckIncludeFile)
include (CheckIncludeFileCXX)
include (CheckFunctionExists)
include (FetchContent)

if (NOT FTL_VERSION)
	set(FTL_VERSION 0.0.2)
endif()

project (beyond-protocol VERSION "${FTL_VERSION}")

include(GNUInstallDirs)
include(CTest)

enable_testing()

option(WITH_GNUTLS "Enable TLS support" OFF)
option(USE_CPPCHECK "Apply cppcheck during build" ON)
option(WITH_OPENSSL "Enable optional OpenSSL features (required by tests)" OFF)
option(BUILD_TESTS "Compile all unit and integration tests" ON)
option(BUILD_EXAMPLES "Compile the examples" ON)
option(ENABLE_PROFILER "Enable builtin performance profil4ing" OFF)
option(DEBUG_LOCKS "Enable lock profiling (requires ENABLE_PROFILER)" OFF)
option(ENABLE_ASAN "Build with address sanitizer" OFF)

if (BUILD_TESTS AND (NOT WITH_OPENSSL))
	message(WARNING "OpenSSL enabled for tests")
	set(WITH_OPENSSL TRUE)
endif()

if (ENABLE_ASAN)
	if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		add_compile_options(-fsanitize=address)
		add_link_options(-fsanitize=address -static-libasan)
	else()
		message(WARNING "Ignoring ENABLE_ASAN for CMAKE_CXX_COMPILER_ID ${CMAKE_CXX_COMPILER_ID}")
		# TODO: MSVC
	endif()
endif()

if (BUILD_TESTS)
	include_directories(src/lib/catch)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

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
	set(HAVE_GNUTLS TRUE)
else()
	add_library(GnuTLS INTERFACE)
	add_library(GnuTLS::GnuTLS ALIAS GnuTLS)
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
		message(SEND_ERROR "UUID library is required")
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

include(ftl_tracy)
include(git_version)
include(ftl_paths)

if (WIN32) # TODO(nick) Should do based upon compiler (VS)
	add_definitions(-DWIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /std:c++17 /wd4996 /Zc:__cplusplus")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /DFTL_DEBUG /Wall")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /W3")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /O2 /W3 /Z7")
	set(OS_LIBS "")
	
	if ("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "x64")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2")
	elseif ("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "ARM64")	
	endif()
	
else() # TODO: just GCC+Linux
	add_definitions(-DUNIX)
	# -fdiagnostics-color, -mfpmath=sse should automatically be set for x86-64
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fPIC -march=haswell -mavx2 -mfpmath=sse -Wall -Werror -Wpointer-arith -Wno-error=unused-but-set-variable -Wno-error=unused-variable -Wno-error=maybe-uninitialized")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -pg")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O3")
	set(OS_LIBS "dl")
endif()

SET(CMAKE_USE_RELATIVE_PATHS ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

### ====================================================================================================================

find_package(MsQuic REQUIRED)
set(HAVE_MSQUIC TRUE)

# ==== OpenSSL =================================================================

if (WITH_OPENSSL)
	set(OPENSSL_USE_STATIC_LIBS TRUE)
	if(WIN32)
		# On Windows also checks if OpenSSL binaries are available with MsQuic 
		if(NOT DEFINED OPENSSL_ROOT_DIR)
			set(OPENSSL_ROOT_DIR "${MsQuic_DIR}/../..")
			find_package(OpenSSL)
		endif()
	endif()
	if (NOT OPENSSL_FOUND)
		find_package(OpenSSL REQUIRED)
	endif()
	set(HAVE_OPENSSL TRUE)
endif()

### ============================================================================

add_subdirectory(src/quic)

### Generate Build Configuration Files =========================================

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/ftl/protocol/config.h.in
			   ${CMAKE_CURRENT_SOURCE_DIR}/include/ftl/protocol/config.h
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp.in
			   ${CMAKE_CURRENT_SOURCE_DIR}/src/config.cpp
)

if (WIN32) # TODO(nick) Should do based upon compiler (VS)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${VS_STARTUP_PROJECT})
	#set_property(TARGET voltu-node PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${VS_DEBUG_WORKING_DIRECTORY})
endif()

include(ftl_CPack)

### ====================================================================================================================

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
	src/common/profiler.cpp
)

target_include_directories(beyond-common PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>)

target_link_libraries(beyond-common PRIVATE
	TracyClient
)

### ====================================================================================================================


add_library(beyond-protocol STATIC
	$<TARGET_OBJECTS:beyond-common>
	$<TARGET_OBJECTS:beyond-quic>

	src/peer.cpp
	src/peer_tcp.cpp
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
	src/streams/packetmanager.cpp

	src/node.cpp
	src/self.cpp
	src/protocol.cpp
	src/rpc.cpp
	src/channelUtils.cpp
	src/service.cpp
	src/codecs/data.cpp

	src/rpc/time.cpp
)

target_include_directories(beyond-protocol PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>)

target_include_directories(beyond-protocol PRIVATE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/quic/include>
)

target_link_libraries(beyond-protocol PRIVATE msquic)

target_link_libraries(beyond-protocol PUBLIC
	Threads::Threads
	${UUID_LIBRARIES}
	TracyClient
	msquic
)

if (WITH_GNUTLS)
	target_link_libraries(beyond-protocol PRIVATE GnuTLS::GnuTLS)
endif()

if(WITH_OPENSSL)
	target_link_libraries(beyond-protocol PRIVATE OpenSSL::Crypto)
endif()

# Install ##############################################################################################################

include(CMakePackageConfigHelpers)

install(
	TARGETS TracyClient
	EXPORT BeyondProtocolTargets
)

install(
	TARGETS beyond-protocol
	EXPORT BeyondProtocolTargets
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
	EXPORT BeyondProtocolTargets
	FILE BeyondProtocolTargets.cmake
	NAMESPACE beyond::
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BeyondProtocol"
)

target_include_directories(beyond-protocol PUBLIC 
	$<INSTALL_INTERFACE:include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_include_directories(beyond-protocol PUBLIC 
	$<INSTALL_INTERFACE:include/ftl/lib>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/ftl/lib>
)

install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

set(INCLUDE_INSTALL_DIR "${CMAKE_INSTALL_INCLUDEDIR}")

configure_package_config_file(
	cmake/BeyondProtocolConfig.cmake.in
	"${CMAKE_CURRENT_BINARY_DIR}/BeyondProtocolConfig.cmake"
	INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BeyondProtocol"
	PATH_VARS INCLUDE_INSTALL_DIR CMAKE_CURRENT_SOURCE_DIR
)

write_basic_package_version_file(
	"BeyondProtocolConfigVersion.cmake"
	VERSION ${FTL_VERSION}
	COMPATIBILITY AnyNewerVersion
)

install(FILES
	"${CMAKE_CURRENT_BINARY_DIR}/BeyondProtocolConfig.cmake"
	"${CMAKE_CURRENT_BINARY_DIR}/BeyondProtocolConfigVersion.cmake"
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/BeyondProtocol"
)

# CMake can be configured directly from build directory
export(
	EXPORT BeyondProtocolTargets
	FILE "${CMAKE_CURRENT_BINARY_DIR}/BeyondProtocolTargets.cmake"
	NAMESPACE beyond::
)

########################################################################################################################

if (BUILD_TESTS)
	add_subdirectory(test)
endif()

if (BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()
