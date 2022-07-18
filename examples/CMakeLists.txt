add_executable(read-ftl-file ./read-ftl-file/main.cpp)
target_link_libraries(read-ftl-file beyond-protocol Threads::Threads ${OS_LIBS} ${URIPARSER_LIBRARIES} ${UUID_LIBRARIES})

add_executable(decode-h264 ./decode-h264/main.cpp)
target_link_libraries(decode-h264 beyond-protocol Threads::Threads ${OS_LIBS} ${URIPARSER_LIBRARIES} ${UUID_LIBRARIES})

add_executable(open-network-stream ./open-network-stream/main.cpp)
target_link_libraries(open-network-stream beyond-protocol Threads::Threads ${OS_LIBS} ${URIPARSER_LIBRARIES} ${UUID_LIBRARIES})

add_executable(create-network-stream ./create-network-stream/main.cpp)
target_link_libraries(create-network-stream beyond-protocol Threads::Threads ${OS_LIBS} ${URIPARSER_LIBRARIES} ${UUID_LIBRARIES})