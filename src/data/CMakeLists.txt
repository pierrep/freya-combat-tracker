# qpdf (Apache-2.0). CMake looks in $HOME/opt/qpdf first, then the default
# search path. The system package may be the runtime library only.
find_package(qpdf CONFIG REQUIRED HINTS "$ENV{HOME}/opt/qpdf")
# Header warnings in the qpdf install must not fail this target's -Werror.
get_target_property(qpdf_includes qpdf::libqpdf INTERFACE_INCLUDE_DIRECTORIES)
if(qpdf_includes)
    set_property(TARGET qpdf::libqpdf PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${qpdf_includes}")
endif()

add_library(combat_data STATIC
    json_campaign.cpp
    json_character_store.cpp
    json_encounters.cpp
    json_history.cpp
    json_monster_codec.cpp
    json_monsters.cpp
    json_sheet.cpp
    pdf_import.cpp
)

target_include_directories(combat_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(combat_data
    PUBLIC combat_core qpdf::libqpdf
    PRIVATE nlohmann_json::nlohmann_json
)
# qpdf warns if this is left unset. 0 keeps the compatible API and skips the warning.
target_compile_definitions(combat_data PUBLIC POINTERHOLDER_TRANSITION=0)
combat_tracker_set_warnings(combat_data)
