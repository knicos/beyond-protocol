add_executable(read-ftl-file ./read-ftl-file/main.cpp)
target_link_libraries(read-ftl-file beyond-protocol Threads::Threads ${OS_LIBS} ${URIPARSER_LIBRARIES} ${UUID_LIBRARIES})