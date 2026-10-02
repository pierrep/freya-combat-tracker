add_library(combat_core STATIC
    character.cpp
    uuid.cpp
    monster.cpp
    monster_catalog.cpp
    encounter.cpp
    sheet.cpp
    combat_rules.cpp
)

target_include_directories(combat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
combat_tracker_set_warnings(combat_core)
