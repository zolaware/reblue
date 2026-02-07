# reblue - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(reblue VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(generated/rexglue.cmake)

set(REBLUE_SOURCES
    src/main.cpp

    # common
    src/bdengine/common/threading.cpp

    # d2anime framework
    src/bdengine/d2anime/anime_layout.cpp
    src/bdengine/d2anime/d2anime.cpp
    src/bdengine/d2anime/d2anime_menu.cpp
    src/bdengine/d2anime/d2anime_task.cpp
    src/bdengine/d2anime/sysmes.cpp

    # title-screen config menu
    src/bdengine/config/config_menu.cpp
    src/bdengine/config/config_menu_data.cpp

    # mods and DLC
    src/bdengine/mods/dlc_manager.cpp
    src/bdengine/mods/mod_manager.cpp
    src/bdengine/mods/vfs.cpp

    # platform integration
    src/bdengine/platform/file_dialogue.cpp
    src/bdengine/platform/folder_dialogue.cpp

    # guest hooks
    src/bdengine/hooks/debug.cpp
    src/bdengine/hooks/math.cpp
    src/bdengine/hooks/misc.cpp
    src/bdengine/hooks/title_menu.cpp

    # thirdparty
    thirdparty/miniz/miniz.cpp)

if(WIN32)
    list(APPEND REBLUE_SOURCES res/windows/reblue.rc res/windows/reblue.manifest)
    set_source_files_properties(res/windows/reblue.rc PROPERTIES
        COMPILE_DEFINITIONS
        "REBLUE_VER_MAJOR=${reblue_VERSION_MAJOR};REBLUE_VER_MINOR=${reblue_VERSION_MINOR};REBLUE_VER_PATCH=${reblue_VERSION_PATCH}")
    add_executable(reblue WIN32 ${REBLUE_SOURCES})
else()
    add_executable(reblue ${REBLUE_SOURCES})
endif()

rexglue_setup_target(reblue)
target_include_directories(reblue PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/miniz)
target_link_libraries(reblue PRIVATE winmm ole32 uuid)

# Enable Tracy guest function profiling (compiled out in Release)
target_compile_definitions(reblue PRIVATE
    $<$<NOT:$<CONFIG:Release>>:REXGLUE_ENABLE_PROFILING>
    $<$<NOT:$<CONFIG:Release>>:REXGLUE_PROFILE_GUEST_FUNCTIONS>
)
