set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/examples/net)

add_executable(pingpong_server pingpong_server.cc)
target_link_libraries(pingpong_server PRIVATE csj_net)

add_executable(pingpong_client pingpong_client.cc)
target_link_libraries(pingpong_client PRIVATE csj_net)

add_executable(bench bench.cc)
target_link_libraries(bench PRIVATE csj_net)

add_executable(tcp_bench tcp_bench.cc)
target_link_libraries(tcp_bench PRIVATE csj_net)

add_executable(http_bench http_bench.cc)
target_link_libraries(http_bench PRIVATE csj_net)
