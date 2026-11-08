cmake_minimum_required(VERSION 3.25)
project(reblue_launcher_tests LANGUAGES CXX)
if(MSVC)
    add_compile_options(/utf-8 /EHsc)
endif()
enable_testing()
add_executable(content_setup_test content_setup_test.cpp ../../src/launcher/content_setup.cpp)
target_include_directories(content_setup_test PRIVATE ../../src)
target_compile_features(content_setup_test PRIVATE cxx_std_20)
add_test(NAME content_setup COMMAND content_setup_test)

option(REBLUE_TEST_WITH_SDK "Build SDK adapters and the optional real-disc probe" OFF)
if(REBLUE_TEST_WITH_SDK)
    find_package(rexglue 0.10.0 CONFIG REQUIRED)
    set(reblue_root "${CMAKE_CURRENT_LIST_DIR}/../..")
    add_executable(disc_probe disc_probe.cpp
        ${reblue_root}/src/launcher/content_setup.cpp
        ${reblue_root}/src/launcher/disc_source.cpp
        ${reblue_root}/src/installer/disc_image.cpp
        ${reblue_root}/src/core/xcontent.cpp)
    target_include_directories(disc_probe PRIVATE ${reblue_root}/src)
    target_compile_definitions(disc_probe PRIVATE REBLUE_BUILD_INSTALLER)
    target_compile_features(disc_probe PRIVATE cxx_std_23)
    target_link_libraries(disc_probe PRIVATE rex::runtime)
    rexglue_apply_target_settings(disc_probe)
    add_executable(cli_test cli_test.cpp
        ${reblue_root}/src/launcher/recomplauncher.cpp
        ${reblue_root}/src/launcher/content_setup.cpp)
    target_include_directories(cli_test PRIVATE ${reblue_root}/src)
    target_compile_features(cli_test PRIVATE cxx_std_23)
    target_link_libraries(cli_test PRIVATE rex::runtime)
    rexglue_apply_target_settings(cli_test)
    add_test(NAME external_cli COMMAND cli_test)
    if(WIN32)
        add_custom_command(TARGET disc_probe POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:rex::runtime> $<TARGET_FILE_DIR:disc_probe>)
    endif()

    # No guest code/assets needed: compile the native boundaries without linking
    # a second game or substituting a fake renderer/installer implementation.
    include(${reblue_root}/cmake/embed.cmake)
    reblue_embed_directory(ROOT "${reblue_root}/res/embed"
        HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded.h"
        SOURCES_VAR unused_embed_sources)
    add_library(sdk_adapters OBJECT
        ${reblue_root}/src/launcher/entry_point.cpp
        ${reblue_root}/src/launcher/recomplauncher.cpp
        ${reblue_root}/src/installer/disc_install.cpp)
    target_include_directories(sdk_adapters PRIVATE
        ${reblue_root}/src "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(sdk_adapters PRIVATE REBLUE_BUILD_INSTALLER
        REBLUE_SDK_ENTRYPOINT="${REXGLUE_SHARE_DIR}/windowed_app_main_sdl.cpp")
    target_compile_features(sdk_adapters PRIVATE cxx_std_23)
    target_link_libraries(sdk_adapters PRIVATE rex::runtime)
    rexglue_apply_target_settings(sdk_adapters)
endif()
