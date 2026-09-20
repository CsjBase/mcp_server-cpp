file(GLOB MCP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.cc" "${CMAKE_CURRENT_SOURCE_DIR}/base/*.cc" "${CMAKE_CURRENT_SOURCE_DIR}/json_rpc/*.cc")

add_library(csj_mcp STATIC ${MCP_SRC})

target_link_libraries(csj_mcp PUBLIC csj_log nlohmann_json::nlohmann_json)
