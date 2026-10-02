find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

add_executable(combat_app
    main.cpp
    app_paths.cpp
    main_window.cpp
    dashboard_page.cpp
    characters_page.cpp
    monsters_page.cpp
    combat_page.cpp
)

# The SRD catalog is read from the source tree. v1 runs from the build directory.
target_compile_definitions(combat_app PRIVATE
    "COMBAT_TRACKER_SRD_DIR=\"${CMAKE_SOURCE_DIR}/data/srd\""
)

set_target_properties(combat_app PROPERTIES
    AUTOMOC ON
    OUTPUT_NAME freya-combat-tracker
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE OFF
)
target_include_directories(combat_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(combat_app PRIVATE combat_core combat_data Qt6::Widgets)
combat_tracker_set_warnings(combat_app)
