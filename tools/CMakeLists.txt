set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tools)

add_executable(log_tool log_tool.cc)
target_link_libraries(log_tool PRIVATE csj_log csj_utils)
