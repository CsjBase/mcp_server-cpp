set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/mcp/transport)

add_executable(StreamableHttpServer_test StreamableHttpServer_test.cc  )
target_link_libraries(StreamableHttpServer_test PRIVATE  csj_mcp)

add_executable(StdioTransport_test StdioTransport_test.cc  )
target_link_libraries(StdioTransport_test PRIVATE  csj_mcp)
