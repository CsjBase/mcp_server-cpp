add_executable(circular_q_test circular_q_test.cc)
target_link_libraries(circular_q_test PRIVATE csj_utils GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(circular_q_test)
