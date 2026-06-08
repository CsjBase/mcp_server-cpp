file(GLOB UTILS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

add_library(csj_utils STATIC ${UTILS_SRC})