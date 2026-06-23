# file(GLOB TEST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

#if(TEST_SRC)
#    add_executable(mcp_server_tests ${TEST_SRC})
#
#    target_link_libraries(mcp_server_tests PRIVATE
#        csj_log
#        csj_net
#        csj_utils
#        GTest::gtest
#        GTest::gtest_main
#    )
#
#    include(GoogleTest)
#    gtest_discover_tests(mcp_server_tests)
#endif()

# add_executable(fmt_test test.cc)
# target_link_libraries(fmt_test PRIVATE fmt::fmt)

add_executable(formatter_test formatter_test.cc)
target_link_libraries(formatter_test PUBLIC csj_log)

add_executable(stdout_log_sink_test stdout_log_sink_test.cc)
target_link_libraries(stdout_log_sink_test PUBLIC csj_log)

add_executable(color_log_sink_test color_log_sink_test.cc)
target_link_libraries(color_log_sink_test PUBLIC csj_log)

add_executable(basic_file_log_sink_test basic_file_log_sink_test.cc)
target_link_libraries(basic_file_log_sink_test PUBLIC csj_log)

add_executable(rotating_file_log_sink_test rotating_file_log_sink_test.cc)
target_link_libraries(rotating_file_log_sink_test PUBLIC csj_log)

add_executable(daily_file_log_sink_test daily_file_log_sink_test.cc)
target_link_libraries(daily_file_log_sink_test PUBLIC csj_log)
