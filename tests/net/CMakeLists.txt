set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/net)

add_executable(basic_EchoServer_test basic_EchoServer_test.cc)
target_link_libraries(basic_EchoServer_test PRIVATE csj_net )

add_executable(timerManager_test timerManager_test.cc)
target_link_libraries(timerManager_test PRIVATE csj_net )

add_executable(echo_TcpServer_test echo_TcpServer_test.cc)
target_link_libraries(echo_TcpServer_test PRIVATE csj_net )

add_executable(http_test http_test.cc)
target_link_libraries(http_test PRIVATE csj_net )

add_executable(http_parser_test http_parser_test.cc)
target_link_libraries(http_parser_test PRIVATE csj_net )

add_executable(http_server_test http_server_test.cc)
target_link_libraries(http_server_test PRIVATE csj_net )

add_executable(echo_bench echo_bench.cc)
target_link_libraries(echo_bench PRIVATE csj_net)
# 基准程序必须带优化编译, 否则数字失真(其余测试保持 Debug)
target_compile_options(echo_bench PRIVATE -O2)
