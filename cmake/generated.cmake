include("${CMAKE_CURRENT_LIST_DIR}/build_info.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/embed.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/shader_cache.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/shaders.cmake")

# A build stamp in target_compile_definitions puts the commit on every TU's
# command line, so every commit would rebuild the whole target.
reblue_write_build_info("${REBLUE_GEN_DIR}/core/build_info.h")

reblue_shader_cache(
    RECOMP_TARGET XenosRecomp
    INPUT_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    INCLUDE_FILE  "${REBLUE_SHADER_COMMON_H}"
    OUTPUT_CPP    "${REBLUE_GEN_DIR}/shader_cache.cpp")
set(REBLUE_GENERATED_SOURCES "${REBLUE_GEN_DIR}/shader_cache.cpp")

foreach(shader IN ITEMS copy_vs bd_2d_blit_vs imgui_vs)
    reblue_host_shader(${shader} vs_6_0)
endforeach()
foreach(shader IN ITEMS
        copy_color_ps copy_depth_ps gamma_correction_ps pfx_occlusion_count_ps
        bd_2d_blit_ps imgui_ps
        resolve_msaa_color_2x resolve_msaa_color_4x resolve_msaa_color_8x
        resolve_msaa_depth_2x resolve_msaa_depth_4x resolve_msaa_depth_8x)
    reblue_host_shader(${shader} ps_6_0)
endforeach()
foreach(shader IN ITEMS bd_pe_ps_brightpass_clamp bd_pe_ps_ms_bright_clamp)
    reblue_host_shader(${shader} ps_6_0 -D REBLUE_RECOMP)
endforeach()

if(REBLUE_BUILD_INSTALLER)
    set(embed_skip "")
else()
    set(embed_skip installer)
endif()
reblue_embed_directory(
    ROOT        "${CMAKE_CURRENT_SOURCE_DIR}/res/embed"
    HEADER      "${REBLUE_GEN_DIR}/embedded.h"
    SOURCES_VAR REBLUE_EMBEDDED_SOURCES
    SKIP_DIRS   ${embed_skip})
list(APPEND REBLUE_GENERATED_SOURCES ${REBLUE_EMBEDDED_SOURCES})

# Neither the recompiled guest code nor these data blobs read REBLUE_D3D12, so
# both exes link one set of objects instead of paying for 54 recomp TUs and a
# multi-megabyte shader cache per backend. OBJECT and never STATIC: an archive
# drops the REX_HOOK registration symbols.
#
# The two halves are separate libraries so the recomp TUs can take the SDK's
# reblue_pch.h, which a shared library would force onto the shader cache and the
# embedded blobs as well.
function(reblue_generated_object_library name)
    add_library(${name} OBJECT ${ARGN})
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${REBLUE_GEN_DIR}"
        "${REXGLUE_ENTRYPOINT_INCLUDE_DIR}")
    target_compile_definitions(${name} PRIVATE
        $<${REBLUE_PROFILING_ON}:REXGLUE_ENABLE_PROFILING>)
    target_link_libraries(${name} PRIVATE
        rex::sdk
        # reblue_init.h reaches for tracy/Tracy.hpp when zones are on, and these
        # objects have to see the same headers the exes compile against.
        $<${REBLUE_PROFILING_ON}:rex::TracyClient>)
    rexglue_apply_target_settings(${name})
endfunction()

# Empty until codegen has run once, and a tree in that state still has to
# configure so the build can generate it.
set(REBLUE_RECOMP_LIB "")
if(REXGLUE_ENTRYPOINT_GENERATED_SOURCES)
    reblue_generated_object_library(reblue_recomp ${REXGLUE_ENTRYPOINT_GENERATED_SOURCES})
    rexglue_apply_recomp_settings(reblue_recomp
        "${REXGLUE_ENTRYPOINT_INCLUDE_DIR}/reblue_pch.h")
    add_dependencies(reblue_recomp reblue_codegen)
    set(REBLUE_RECOMP_LIB reblue_recomp)
endif()

reblue_generated_object_library(reblue_generated ${REBLUE_GENERATED_SOURCES})
add_dependencies(reblue_generated reblue_shader_cache_gen)

# rexglue_setup_target adds the entrypoint sources to whatever target it is
# called on, which would compile them again into each exe.
set(REXGLUE_ENTRYPOINT_GENERATED_SOURCES "")

# reblue_prelink DXC-links every specConstantsMask-subset variant of the shader
# cache at build time, so the runtime never pays a DXC link for a shipped shader.
if(REBLUE_D3D12_TARGETS)
    add_executable(reblue_prelink
        tools/prelink_shader_cache.cpp
        src/gpu/shaders/dxc_link.cpp
        "${REBLUE_GEN_DIR}/shader_cache.cpp"
        thirdparty/miniz/miniz.cpp
        thirdparty/zstd/zstddeclib.c)
    add_dependencies(reblue_prelink reblue_shader_cache_gen)
    target_include_directories(reblue_prelink PRIVATE src thirdparty/miniz thirdparty/zstd)
    target_link_libraries(reblue_prelink PRIVATE Microsoft::DirectXShaderCompiler)
    reblue_stage_dxc(reblue_prelink)

    add_custom_command(
        OUTPUT "${REBLUE_GEN_DIR}/linked_shader_cache.cpp"
        COMMAND reblue_prelink "${REBLUE_GEN_DIR}/linked_shader_cache.cpp"
        DEPENDS reblue_prelink
        COMMENT "Pre-linking spec-constant shader variants"
        VERBATIM)
    set_source_files_properties("${REBLUE_GEN_DIR}/linked_shader_cache.cpp"
        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    foreach(target IN LISTS REBLUE_D3D12_TARGETS)
        target_sources(${target} PRIVATE "${REBLUE_GEN_DIR}/linked_shader_cache.cpp")
    endforeach()
endif()
