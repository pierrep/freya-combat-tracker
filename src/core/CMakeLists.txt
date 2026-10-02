add_library(combat_core STATIC
    character.cpp
    uuid.cpp
)

target_include_directories(combat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
combat_tracker_set_warnings(combat_core)
