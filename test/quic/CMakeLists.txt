add_executable(quic_api_unit
	$<TARGET_OBJECTS:CatchTest>
	./quic_api.cpp)

set(QUIC_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/quic/src")

target_include_directories(quic_api_unit PRIVATE ${QUIC_INCLUDE_DIRS})

target_link_libraries(quic_api_unit
	beyond-quic
	beyond-protocol
	Threads::Threads
	${OS_LIBS}
	${URIPARSER_LIBRARIES}
)

add_test(MsQuicApiTest quic_api_unit)

########################################################################################################################

add_executable(quic_peer_unit
	$<TARGET_OBJECTS:CatchTest>
	./quic_peer_test.cpp)

target_include_directories(quic_peer_unit PRIVATE ${QUIC_INCLUDE_DIRS})

target_link_libraries(quic_peer_unit
	beyond-quic
	beyond-protocol
	Threads::Threads
	${OS_LIBS}
	${URIPARSER_LIBRARIES}
)

add_test(QuicPeerTest quic_peer_unit)

########################################################################################################################
add_executable(websocket_frame_unit
	./websocket_frame_unit.cpp
	../../src/loguru.cpp)
target_include_directories(websocket_frame_unit PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/../../include"
	${QUIC_INCLUDE_DIRS})
target_link_libraries(websocket_frame_unit
	$<TARGET_OBJECTS:CatchTest>
	Threads::Threads ${OS_LIBS})

add_test(WebsocketFrameTest websocket_frame_unit)
