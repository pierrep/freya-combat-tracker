find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

# The pages are a library so the offscreen UI tests can drive them.
add_library(combat_ui STATIC
    app_paths.cpp
    main_window.cpp
    characters_page.cpp
    monsters_page.cpp
    combat_page.cpp
    encounter_builder_page.cpp
    theme.cpp
    options_page.cpp
    effects_dialog.cpp
    flow_layout.cpp
)
set_target_properties(combat_ui PROPERTIES AUTOMOC ON)
target_include_directories(combat_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(combat_ui PUBLIC combat_core combat_data Qt6::Widgets)
# A build tree reads the SRD catalog from the source tree. An installed or
# copied app finds it beside the executable first (see app_paths.h).
target_compile_definitions(combat_ui PRIVATE
    "COMBAT_TRACKER_SRD_DIR=\"${CMAKE_SOURCE_DIR}/data/srd\""
)
combat_tracker_set_warnings(combat_ui)

add_executable(combat_app main.cpp)
set_target_properties(combat_app PROPERTIES
    OUTPUT_NAME freya-combat-tracker
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE OFF
)
target_link_libraries(combat_app PRIVATE combat_ui)
combat_tracker_set_warnings(combat_app)

include(GNUInstallDirs)
install(TARGETS combat_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    ${CMAKE_SOURCE_DIR}/data/srd/monsters.json
    ${CMAKE_SOURCE_DIR}/data/srd/spells.json
    ${CMAKE_SOURCE_DIR}/data/srd/conditions.json
    ${CMAKE_SOURCE_DIR}/data/srd/species.json
    ${CMAKE_SOURCE_DIR}/data/srd/ATTRIBUTION.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/freya-combat-tracker/srd)
