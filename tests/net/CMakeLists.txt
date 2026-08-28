set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/net)

add_executable(basic_EchoServer_test basic_EchoServer_test.cc)
target_link_libraries(basic_EchoServer_test PRIVATE csj_net )
