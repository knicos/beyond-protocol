find_package(MsgPack REQUIRED)
find_package(MsQuic REQUIRED)

if(WIN32)
	# TODO: Why aren't include directories correctly set up on Windows (when built with CMake)?
	#       This will only work for installed target (can't use build directory directly).
	target_include_directories(msquic INTERFACE "${MsQuic_DIR}/../../include")
	if(NOT EXISTS "${MsQuic_DIR}/../../include")
		message(SEND_ERROR "msquic.h not found. Is MsQuic installed?")
	endif()
endif()

add_library(beyond-quic OBJECT
	src/msquic/quic.cpp
	src/msquic/connection.cpp
	src/msquic/stream.cpp
	src/proxy.cpp
	src/quic_peer.cpp
	src/quic_universe.cpp
	src/openssl_util.cpp
)

target_include_directories(beyond-quic PRIVATE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include/ftl/lib>
)

if(WITH_OPENSSL)
	target_link_libraries(beyond-quic PRIVATE OpenSSL::Crypto)
	if (WIN32)
		#target_link_libraries(beyond-quic PRIVATE OpenSSL::applink)
	endif()
endif()

target_include_directories(beyond-quic PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

target_link_libraries(beyond-quic PRIVATE
	TracyClient
)

target_link_libraries(beyond-quic PUBLIC
	msquic beyond-common
)
