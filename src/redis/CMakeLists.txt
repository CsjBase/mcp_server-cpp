file(GLOB REDIS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

add_library(csj_redis STATIC ${REDIS_SRC})

target_link_libraries(csj_redis PUBLIC hiredis::hiredis csj_log csj_net)
