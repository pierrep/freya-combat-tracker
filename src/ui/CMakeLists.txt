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
    dice_overlay.cpp
)
set_target_properties(combat_ui PROPERTIES AUTOMOC ON)
target_include_directories(combat_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(combat_ui PUBLIC combat_core combat_data Qt6::Widgets)
# This binary looks in data/srd first, then beside the executable, so a catalog
# edit is what a build-tree run loads (see app_paths.h).
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

# The window icon, built in so it shows without an install. Large and small
# drawings: the d20's facets drop out at 32 pixels and below.
set(FREYA_ICON_DIR ${CMAKE_SOURCE_DIR}/data/icons/hicolor)
set(FREYA_ICON_SIZES 16 22 24 32 48 64 128 256)
set(FREYA_ICON_FILES "")
foreach(size IN LISTS FREYA_ICON_SIZES)
    list(APPEND FREYA_ICON_FILES ${FREYA_ICON_DIR}/${size}x${size}/apps/freya-combat-tracker.png)
endforeach()
qt_add_resources(combat_app "app_icon"
    PREFIX "/icons"
    BASE ${FREYA_ICON_DIR}
    FILES ${FREYA_ICON_FILES}
)

include(GNUInstallDirs)
install(TARGETS combat_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# The desktop entry and icons, so the app appears in the menu and dock with
# its icon (cmake --install <build> --prefix ~/.local for one user).
if(UNIX AND NOT APPLE)
    install(FILES ${CMAKE_SOURCE_DIR}/data/freya-combat-tracker.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(size IN LISTS FREYA_ICON_SIZES)
        install(FILES ${FREYA_ICON_DIR}/${size}x${size}/apps/freya-combat-tracker.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps)
    endforeach()
    install(FILES ${FREYA_ICON_DIR}/scalable/apps/freya-combat-tracker.svg
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
install(FILES
    ${CMAKE_SOURCE_DIR}/data/srd/monsters.json
    ${CMAKE_SOURCE_DIR}/data/srd/spells.json
    ${CMAKE_SOURCE_DIR}/data/srd/conditions.json
    ${CMAKE_SOURCE_DIR}/data/srd/species.json
    ${CMAKE_SOURCE_DIR}/data/srd/ATTRIBUTION.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/freya-combat-tracker/srd)
