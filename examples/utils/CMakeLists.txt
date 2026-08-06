set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/examples/utils)

add_executable(zlib_compress_example zlib_compress_example.cc)
target_link_libraries(zlib_compress_example PRIVATE csj_utils)

add_executable(aes_crypt_example aes_crypt_example.cc)
target_link_libraries(aes_crypt_example PRIVATE csj_log csj_utils)
