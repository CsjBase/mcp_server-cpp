set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/examples/logger)

add_executable(example example.cc)
target_link_libraries(example PRIVATE csj_log)

