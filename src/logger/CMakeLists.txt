file(GLOB LOG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

add_library(csj_log STATIC ${LOG_SRC})

target_link_libraries(csj_log PUBLIC csj_utils fmt::fmt)
