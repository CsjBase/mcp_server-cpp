file(GLOB MCP_SRC "${CMAKE_SOURCE_DIR}/src/mcp/*.cc")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/mcp)

add_executable(stdio_jsonrpc_test stdio_jsonrpc_test.cc  ${MCP_SRC})
target_link_libraries(stdio_jsonrpc_test PRIVATE csj_log)