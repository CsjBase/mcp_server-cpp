set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/mcp)

add_subdirectory(json_rpc)
add_subdirectory(transport)
add_subdirectory(server)



add_executable(echo_server_stdio echo_server_stdio.cc  )
target_link_libraries(echo_server_stdio PRIVATE  csj_mcp)

add_executable(full_server_http full_server_http.cc  )
target_link_libraries(full_server_http PRIVATE  csj_mcp)
