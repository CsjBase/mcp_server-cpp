set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/mcp/server)

add_executable(server_tests
test_prompts.cpp
test_resources.cpp
test_tools.cpp
test_uri_template.cpp
)
target_link_libraries(server_tests
    PRIVATE csj_mcp GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(server_tests)