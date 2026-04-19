# bin2c helper: embeds an arbitrary binary file as a C++ uint8_t[] + size_t.
#
# Usage:
#   include(cmake/bin2c.cmake)
#   reblue_bin2c(INPUT res/installer/foo.png SYMBOL g_foo_png STEM ${CMAKE_CURRENT_BINARY_DIR}/generated/foo_png)
#
# Emits <STEM>.h and <STEM>.cpp declaring:
#   extern const size_t  <SYMBOL>_size;
#   extern const uint8_t <SYMBOL>_data[];
function(reblue_bin2c)
    set(oneValueArgs INPUT SYMBOL STEM)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "" ${ARGN})
    if(NOT ARG_INPUT OR NOT ARG_SYMBOL OR NOT ARG_STEM)
        message(FATAL_ERROR "reblue_bin2c requires INPUT, SYMBOL, STEM")
    endif()

    set(_header "${ARG_STEM}.h")
    set(_source "${ARG_STEM}.cpp")
    set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/bin2c_run.cmake")

    add_custom_command(
        OUTPUT "${_header}" "${_source}"
        COMMAND ${CMAKE_COMMAND}
                -DINPUT=${ARG_INPUT}
                -DSYMBOL=${ARG_SYMBOL}
                -DSTEM=${ARG_STEM}
                -P ${_script}
        DEPENDS "${ARG_INPUT}" "${_script}"
        COMMENT "Embedding ${ARG_INPUT} as ${ARG_SYMBOL}"
        VERBATIM)

    set_source_files_properties("${_header}" "${_source}" PROPERTIES GENERATED TRUE)
endfunction()
