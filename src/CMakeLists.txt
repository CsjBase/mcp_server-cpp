add_subdirectory(utils)
add_subdirectory(logger)
add_subdirectory(net)

target_include_directories(csj_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(csj_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(csj_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})