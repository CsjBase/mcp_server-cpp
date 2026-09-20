set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/mcp/json_rpc)

add_executable(jsonrpc_types_test jsonrpc_types_test.cc  )
target_link_libraries(jsonrpc_types_test PRIVATE  csj_mcp nlohmann_json::nlohmann_json)


add_executable(mcp_tests
    test_jsonrpc_types.cpp
    test_input_validator.cpp
    test_default_request_context.cpp
    test_method_dispatcher.cpp
    test_mcp_method_handlers.cpp
)
target_link_libraries(mcp_tests
    PRIVATE csj_mcp GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(mcp_tests)