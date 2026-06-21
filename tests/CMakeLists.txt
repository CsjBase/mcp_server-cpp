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


add_subdirectory(logger)

add_executable(ctest test.cc)
target_link_libraries(ctest PUBLIC csj_log)

