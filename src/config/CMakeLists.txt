file(GLOB CONFIG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")

add_library(csj_config STATIC ${CONFIG_SRC})

target_link_libraries(csj_config PUBLIC nlohmann_json::nlohmann_json)
