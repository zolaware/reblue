# Runs the Xenos shader recompiler over INPUT_DIR at build time into OUTPUT_CPP.
# Defines reblue_shader_cache_gen, and reblue_shader_hlsl_dump for inspecting
# the intermediate HLSL.
function(reblue_shader_cache)
    cmake_parse_arguments(ARG "" "RECOMP_TARGET;INPUT_DIR;INCLUDE_FILE;OUTPUT_CPP" "" ${ARGN})
    foreach(arg RECOMP_TARGET INPUT_DIR INCLUDE_FILE OUTPUT_CPP)
        if(NOT ARG_${arg})
            message(FATAL_ERROR "reblue_shader_cache: missing ${arg}")
        endif()
    endforeach()

    cmake_path(GET ARG_OUTPUT_CPP PARENT_PATH output_dir)
    file(MAKE_DIRECTORY "${output_dir}")

    file(GLOB_RECURSE shader_inputs CONFIGURE_DEPENDS
        "${ARG_INPUT_DIR}/*.vso" "${ARG_INPUT_DIR}/*.pso" "${ARG_INPUT_DIR}/*.xex")

    # The glob above is allowed to come back empty, and the build then succeeds:
    # it just produces an executable with no recompiled guest shaders in it,
    # which draws the menus and the text and nothing else. That is a two-day
    # hunt for whoever builds from a fresh clone, because assets/ is gitignored
    # and the tree still configures and compiles clean. Name the reason instead.
    if(NOT REBLUE_ALLOW_EMPTY_SHADER_CACHE)
        set(shader_blobs "")
        foreach(entry IN LISTS shader_inputs)
            if(entry MATCHES "\.(vso|pso)$")
                list(APPEND shader_blobs "${entry}")
            endif()
        endforeach()
        if(NOT shader_blobs)
            message(FATAL_ERROR
                "No Xenos shader blobs (*.vso / *.pso) under ${ARG_INPUT_DIR}.\n"
                "assets/ is gitignored: CI stages it by cloning the asset repo "
                "(see .github/scripts/fetch-deps.sh), and from your own discs "
                "the BD_SYSTEM/FX and HC_SYSTEM/FX blobs go there by hand, "
                "alongside the default.xex from your own copy of the game. "
                "A build without them runs and draws nothing but menus. "
                "Pass -DREBLUE_ALLOW_EMPTY_SHADER_CACHE=ON to build anyway.")
        endif()
    endif()

    add_custom_command(
        OUTPUT "${ARG_OUTPUT_CPP}"
        COMMAND $<TARGET_FILE:${ARG_RECOMP_TARGET}>
                "${ARG_INPUT_DIR}" "${ARG_OUTPUT_CPP}" "${ARG_INCLUDE_FILE}"
        DEPENDS ${ARG_RECOMP_TARGET} "${ARG_INCLUDE_FILE}" ${shader_inputs}
        COMMENT "Recompiling Xenos shaders from ${ARG_INPUT_DIR}"
        USES_TERMINAL VERBATIM)
    add_custom_target(reblue_shader_cache_gen DEPENDS "${ARG_OUTPUT_CPP}")

    set(REBLUE_HLSL_DUMP_DIR "${CMAKE_BINARY_DIR}/hlsl_dump" CACHE PATH
        "Directory reblue_shader_hlsl_dump writes recompiled HLSL into")
    add_custom_target(reblue_shader_hlsl_dump
        COMMAND ${CMAKE_COMMAND} -E make_directory "${REBLUE_HLSL_DUMP_DIR}"
        COMMAND $<TARGET_FILE:${ARG_RECOMP_TARGET}>
                "${ARG_INPUT_DIR}" "${output_dir}/shader_cache.hlsldump.cpp"
                "${ARG_INCLUDE_FILE}" "${REBLUE_HLSL_DUMP_DIR}"
        COMMENT "Dumping recompiled HLSL to ${REBLUE_HLSL_DUMP_DIR}"
        USES_TERMINAL VERBATIM)
    add_dependencies(reblue_shader_hlsl_dump ${ARG_RECOMP_TARGET})
endfunction()
