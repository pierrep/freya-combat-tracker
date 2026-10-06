add_executable(core_tests
    core/core_tests.cpp
    core/monster_tests.cpp
    core/encounter_tests.cpp
    core/sheet_tests.cpp
    core/combat_rules_tests.cpp
    core/attack_damage_tests.cpp
)
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_tests PRIVATE combat_core)
combat_tracker_set_warnings(core_tests)
add_test(NAME core_tests COMMAND core_tests)

add_executable(data_tests
    data/data_tests.cpp
    data/monster_data_tests.cpp
    data/encounter_data_tests.cpp
    data/sheet_data_tests.cpp
    data/pdf_import_tests.cpp
)
target_include_directories(data_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(data_tests PRIVATE combat_data)
target_compile_definitions(data_tests PRIVATE
    "COMBAT_TRACKER_SRD_DIR=\"${CMAKE_SOURCE_DIR}/data/srd\""
)
combat_tracker_set_warnings(data_tests)
add_test(NAME data_tests COMMAND data_tests)

if(COMBAT_TRACKER_BUILD_APP)
    # Drives the real pages without a display.
    add_executable(ui_tests ui/ui_tests.cpp)
    target_include_directories(ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ui_tests PRIVATE combat_ui)
    target_compile_definitions(ui_tests PRIVATE
        "COMBAT_TRACKER_SRD_DIR=\"${CMAKE_SOURCE_DIR}/data/srd\""
    )
    combat_tracker_set_warnings(ui_tests)
    add_test(NAME ui_tests COMMAND ui_tests)
    set_tests_properties(ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
