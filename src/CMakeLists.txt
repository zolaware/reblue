file(GLOB_RECURSE reblue_all RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     CONFIGURE_DEPENDS "*.cpp" "*.h")

set(reblue_backend_only
    gpu/device.cpp
    gpu/device_lost.cpp
    gpu/device_pipelines.cpp
    gpu/draw.cpp
    gpu/draw_framebuffer.cpp
    gpu/dred.cpp
    gpu/frame_ring.cpp
    gpu/gpu_profiling.cpp
    gpu/gpu_timing.cpp
    gpu/hooks/shader.cpp
    gpu/imgui_overlay_drawer.cpp
    gpu/occlusion.cpp
    gpu/pipeline/pipeline_cache.cpp
    gpu/present.cpp
    gpu/resolve.cpp
    gpu/shaders/guest_shaders.cpp
    gpu/vertex_declaration.cpp
    platform/crash_handler.cpp
    reblue_app.cpp)

# The runtime DXC shader linker has no Vulkan counterpart.
set(reblue_d3d12_only
    gpu/shaders/dxc_link.cpp
    gpu/shaders/dxc_link.h
    gpu/shaders/shader_linker.cpp
    gpu/shaders/shader_linker.h)

set(reblue_installer_only
    installer/disc_install.cpp
    installer/disc_install.h
    installer/installer_wizard.cpp
    installer/installer_wizard.h
    installer/self_install.cpp
    installer/self_install.h
    installer/upgrade_prompt.cpp
    installer/upgrade_prompt.h)

list(REMOVE_ITEM reblue_all
     ${reblue_backend_only} ${reblue_d3d12_only} ${reblue_installer_only})

reblue_sources(${reblue_all})
reblue_backend_sources(${reblue_backend_only})
reblue_d3d12_sources(${reblue_d3d12_only})
if(REBLUE_BUILD_INSTALLER)
    reblue_sources(${reblue_installer_only})
endif()
