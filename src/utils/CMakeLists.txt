file(GLOB UTILS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

add_library(csj_utils STATIC ${UTILS_SRC})

target_include_directories(csj_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})