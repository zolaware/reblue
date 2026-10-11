function(reblue_write_build_info out_header)
    set(REBLUE_GIT_COMMIT "unknown")
    set(REBLUE_GIT_BRANCH "unknown")
    set(REBLUE_GIT_DIRTY 0)

    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --absolute-git-dir
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE git_dir OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET RESULT_VARIABLE git_rc)
    endif()

    if(GIT_FOUND AND git_rc EQUAL 0)
        # Reconfigure when the commit or the working tree changes, so a stamped
        # build never reports a stale commit.
        foreach(dep HEAD index)
            if(EXISTS "${git_dir}/${dep}")
                set_property(DIRECTORY APPEND PROPERTY
                    CMAKE_CONFIGURE_DEPENDS "${git_dir}/${dep}")
            endif()
        endforeach()

        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=9 HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} ERROR_QUIET
            OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} ERROR_QUIET
            OUTPUT_VARIABLE branch OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} ERROR_QUIET
            OUTPUT_VARIABLE worktree_status)

        if(commit)
            set(REBLUE_GIT_COMMIT "${commit}")
        endif()
        if(branch)
            set(REBLUE_GIT_BRANCH "${branch}")
        endif()
        if(worktree_status)
            set(REBLUE_GIT_DIRTY 1)
        endif()
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" arch)
    if(arch MATCHES "amd64|x86_64")
        set(arch amd64)
    elseif(arch MATCHES "aarch64|arm64")
        set(arch arm64)
    endif()
    if(WIN32)
        set(REBLUE_BUILD_PLATFORM "win-${arch}")
    elseif(APPLE)
        set(REBLUE_BUILD_PLATFORM "mac-${arch}")
    else()
        set(REBLUE_BUILD_PLATFORM "linux-${arch}")
    endif()

    set(REBLUE_BUILD_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    string(TIMESTAMP REBLUE_BUILD_TIMESTAMP "%Y%m%d_%H%M")

    # Empty unless the build supplies one, which leaves the check off for
    # local builds and keeps the endpoint out of the source tree.
    if(NOT DEFINED REBLUE_UPDATE_BASE)
        set(REBLUE_UPDATE_BASE "")
    endif()
    if(NOT DEFINED REBLUE_UPDATE_CHANNEL)
        set(REBLUE_UPDATE_CHANNEL "stable")
    endif()

    # Nightlies append a run counter so consecutive ones compare as newer.
    # Every other build reports the plain project version.
    if(NOT DEFINED REBLUE_VERSION_SUFFIX)
        set(REBLUE_VERSION_SUFFIX "")
    endif()

    if(NOT DEFINED REBLUE_OFFICIAL_BUILD)
        set(REBLUE_OFFICIAL_BUILD 0)
    endif()

    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/build_info.h.in" "${out_header}" @ONLY)

    message(STATUS "reblue v${reblue_VERSION}${REBLUE_VERSION_SUFFIX} ${REBLUE_GIT_COMMIT} on "
                   "${REBLUE_GIT_BRANCH} (dirty=${REBLUE_GIT_DIRTY}, official=${REBLUE_OFFICIAL_BUILD}) "
                   "${REBLUE_BUILD_PLATFORM}")
endfunction()
