set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/examples/todos-server)

add_executable(todos-server main.cc register_tools.cc register_resources.cc register_prompts.cc)
target_link_libraries(todos-server PRIVATE csj_mcp csj_db)

