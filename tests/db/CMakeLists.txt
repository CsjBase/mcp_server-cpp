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

include(GoogleTest)
gtest_discover_tests(mysql_test)
gtest_discover_tests(dbpool_concurrent_test)