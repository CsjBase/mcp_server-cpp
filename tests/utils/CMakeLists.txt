add_executable(circular_q_test circular_q_test.cc)
target_link_libraries(circular_q_test PRIVATE csj_utils GTest::gtest GTest::gtest_main)

add_executable(mpmc_blocking_queue_test mpmc_blocking_queue_test.cc)
target_link_libraries(mpmc_blocking_queue_test PRIVATE csj_utils GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(circular_q_test)
gtest_discover_tests(mpmc_blocking_queue_test)
