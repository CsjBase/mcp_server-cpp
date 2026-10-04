set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/redis)

add_executable(redis_test redis_test.cc)
target_link_libraries(redis_test PRIVATE csj_redis GTest::gtest_main)

add_executable(async_redis_test async_redis_test.cc)
target_link_libraries(async_redis_test PRIVATE csj_redis GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(redis_test)
gtest_discover_tests(async_redis_test)
