file(GLOB DB_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

add_library(csj_db STATIC ${DB_SRC})

target_link_libraries(csj_db PUBLIC unofficial::libmysql::libmysql csj_log)
