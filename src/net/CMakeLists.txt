file(GLOB NET_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc" "${CMAKE_CURRENT_SOURCE_DIR}/base/*.cc")

add_library(csj_net STATIC ${NET_SRC})

target_link_libraries(csj_net PUBLIC csj_log)
