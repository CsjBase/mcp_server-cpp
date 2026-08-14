set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/utils)

add_executable(circular_q_test circular_q_test.cc)
target_link_libraries(circular_q_test PRIVATE csj_utils GTest::gtest GTest::gtest_main)

add_executable(mpmc_blocking_queue_test mpmc_blocking_queue_test.cc)
target_link_libraries(mpmc_blocking_queue_test PRIVATE csj_utils GTest::gtest GTest::gtest_main)

add_executable(zlib_compress_test zlib_compress_test.cc)
target_link_libraries(zlib_compress_test PRIVATE csj_utils GTest::gtest GTest::gtest_main)

add_executable(aes_crypt_test aes_crypt_test.cc)
target_link_libraries(aes_crypt_test PRIVATE csj_utils GTest::gtest GTest::gtest_main)

add_executable(thread_pool_test thread_pool_test.cc)
target_link_libraries(thread_pool_test PRIVATE csj_utils GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(circular_q_test)
gtest_discover_tests(mpmc_blocking_queue_test)
gtest_discover_tests(zlib_compress_test)
gtest_discover_tests(aes_crypt_test)
gtest_discover_tests(thread_pool_test)
