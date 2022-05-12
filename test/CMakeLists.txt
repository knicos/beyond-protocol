# Catch test with thread pool cleanup in main()
add_library(CatchTestFTL OBJECT ./tests_ftl.cpp)
target_link_libraries(CatchTestFTL beyond-protocol)

# Default catch test (catch generated main())
add_library(CatchTest OBJECT ./tests.cpp)
target_link_libraries(CatchTest beyond-protocol)

### URI ########################################################################
add_executable(uri_unit
	$<TARGET_OBJECTS:CatchTest>
	./uri_unit.cpp)
target_include_directories(uri_unit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(uri_unit beyond-protocol
	Threads::Threads ${OS_LIBS}
	${URIPARSER_LIBRARIES})

add_test(URIUnitTest uri_unit)

### Handle Unit ################################################################
add_executable(handle_unit
	$<TARGET_OBJECTS:CatchTest>
	./handle_unit.cpp
)
target_include_directories(handle_unit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(handle_unit beyond-protocol
	Threads::Threads ${OS_LIBS}
	${URIPARSER_LIBRARIES})

add_test(HandleUnitTest handle_unit)

### URI ########################################################################
add_executable(net_integration
	$<TARGET_OBJECTS:CatchTestFTL>
	./net_integration.cpp)
target_include_directories(net_integration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(net_integration beyond-protocol
	Threads::Threads ${OS_LIBS}
	${URIPARSER_LIBRARIES})

add_test(NetIntegrationTest net_integration)

### Net performance ############################################################
add_executable(net_performance
	$<TARGET_OBJECTS:CatchTestFTL>
	./net_performance.cpp)
target_include_directories(net_performance PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}../include")
target_link_libraries(net_performance beyond-protocol
	GnuTLS::GnuTLS Threads::Threads ${UUID_LIBRARIES} ${URIPARSER_LIBRARIES} ${OS_LIBS})

add_test(NetPerformance net_performance)

### Muxer Unit #################################################################
add_executable(muxer_unit
	$<TARGET_OBJECTS:CatchTestFTL>
	./muxer_unit.cpp
)
target_include_directories(muxer_unit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(muxer_unit
	beyond-protocol ${URIPARSER_LIBRARIES} ${OS_LIBS})

add_test(MuxerUnitTest muxer_unit)

### Broadcast Unit #############################################################
add_executable(broadcast_unit
	$<TARGET_OBJECTS:CatchTestFTL>
	./broadcast_unit.cpp
)
target_include_directories(broadcast_unit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(broadcast_unit
	beyond-protocol ${URIPARSER_LIBRARIES} ${OS_LIBS})

add_test(BroadcastUnitTest broadcast_unit)

### Stream Integration #########################################################
add_executable(stream_integration
	$<TARGET_OBJECTS:CatchTestFTL>
	./stream_integration.cpp
)
target_include_directories(stream_integration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(stream_integration
	beyond-protocol GnuTLS::GnuTLS Threads::Threads ${URIPARSER_LIBRARIES} ${UUID_LIBRARIES} ${OS_LIBS})

add_test(StreamIntegrationTest stream_integration)

### Webservice E2E #############################################################
add_executable(webservice_e2e
	$<TARGET_OBJECTS:CatchTestFTL>
	./webservice_e2e.cpp
)
target_include_directories(webservice_e2e PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(webservice_e2e
	beyond-protocol GnuTLS::GnuTLS Threads::Threads ${URIPARSER_LIBRARIES} ${UUID_LIBRARIES} ${OS_LIBS})

add_test(WebserviceE2ETest webservice_e2e)