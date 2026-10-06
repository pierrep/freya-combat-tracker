cmake_minimum_required(VERSION 3.25)

project(FreyaCombatTracker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(COMBAT_TRACKER_BUILD_APP "Build the Qt desktop app" ON)
option(COMBAT_TRACKER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

function(combat_tracker_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(COMBAT_TRACKER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
        if(COMBAT_TRACKER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_subdirectory(src/core)
add_subdirectory(src/data)

if(COMBAT_TRACKER_BUILD_APP)
    add_subdirectory(src/ui)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
