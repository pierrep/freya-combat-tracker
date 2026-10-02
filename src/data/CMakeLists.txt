add_library(combat_data STATIC
    json_character_store.cpp
    json_monsters.cpp
    json_encounters.cpp
)

target_include_directories(combat_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(combat_data
    PUBLIC combat_core
    PRIVATE nlohmann_json::nlohmann_json
)
combat_tracker_set_warnings(combat_data)
