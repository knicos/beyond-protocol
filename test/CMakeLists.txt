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
	Threads::Threads ${OS_LIBS})

add_test(HandleUnitTest handle_unit)
