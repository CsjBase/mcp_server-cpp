set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/config)

add_executable(config_test config_test.cc)
target_link_libraries(config_test PRIVATE csj_config )