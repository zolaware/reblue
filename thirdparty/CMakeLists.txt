if(REBLUE_D3D12)
    set(PLUME_D3D12_AGILITY_SDK_ENABLED ON CACHE BOOL "" FORCE)
endif()
# Linux picks X11 or Wayland at runtime, so plume builds its surface through the
# SDL3 window the SDK already owns. rexglue.cmake has imported SDL3 by now, so
# plume finds that target rather than a second copy on the system.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLUME_SDL_VULKAN_ENABLED ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(plume EXCLUDE_FROM_ALL)

add_subdirectory(XenosRecomp EXCLUDE_FROM_ALL)
target_compile_definitions(XenosRecomp PRIVATE REBLUE_RECOMP)
target_precompile_headers(XenosRecomp PRIVATE <fmt/format.h>)

if(NOT TARGET xxHash::xxhash)
    set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
    set(build_shared_prev ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(XenosRecomp/thirdparty/xxHash/cmake_unofficial EXCLUDE_FROM_ALL)
    set(BUILD_SHARED_LIBS ${build_shared_prev} CACHE BOOL "" FORCE)
endif()

add_library(reblue_thirdparty OBJECT
    miniz/miniz.cpp
    stb/stb_image.cpp
    zstd/zstddeclib.c)

if(REBLUE_VULKAN_TARGETS)
    target_sources(reblue_thirdparty PRIVATE
        XenosRecomp/thirdparty/smol-v/source/smolv.cpp)
endif()

target_include_directories(reblue_thirdparty PUBLIC
    miniz
    picosha2
    stb
    zstd
    XenosRecomp/thirdparty/smol-v/source)
target_compile_options(reblue_thirdparty PRIVATE -w)
target_link_libraries(reblue_thirdparty PRIVATE rex::runtime)
