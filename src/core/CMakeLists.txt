add_library(combat_core STATIC
    attack_damage.cpp
    character.cpp
    campaign.cpp
    combat_rules.cpp
    dice.cpp
    encounter.cpp
    encounter_difficulty.cpp
    monster.cpp
    monster_catalog.cpp
    sheet.cpp
    spell_rules.cpp
    stat_block_reader.cpp
    text.cpp
    uuid.cpp
)

target_include_directories(combat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
combat_tracker_set_warnings(combat_core)
