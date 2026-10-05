set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/tests/db)

add_executable(mysql_test mysql_test.cc)
target_link_libraries(mysql_test PRIVATE csj_db GTest::gtest_main
)

add_executable(dbpool_test dbpool_test.cc)
target_link_libraries(dbpool_test PRIVATE csj_db
)

add_executable(dbpool_concurrent_test dbpool_concurrent_test.cc)
target_link_libraries(dbpool_concurrent_test PRIVATE csj_db GTest::gtest_main
)

add_executable(dbpool_bench dbpool_bench.cc)
target_link_libraries(dbpool_bench PRIVATE csj_db)
# 基准程序必须带优化编译, 否则数字失真(其余测试保持 Debug)
target_compile_options(dbpool_bench PRIVATE -O2)

include(GoogleTest)
gtest_discover_tests(mysql_test)
gtest_discover_tests(dbpool_concurrent_test)