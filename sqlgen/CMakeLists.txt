cmake_minimum_required (VERSION 3.21)

project ("sqlgen"
    LANGUAGES C
    VERSION 0.0.1)

add_executable (sqlgen
    "sqlgen.c")

macro (sqlgen_target name)
    set (sqlgen_target_PARAM_OPTIONS)
    set (sqlgen_target_PARAM_ONE_VALUE_KEYWORDS
        INPUT
        HEADER
        SOURCE)
    set (sqlgen_target_PARAM_MULTI_VALUE_KEYWORDS
        BACKENDS)
    cmake_parse_arguments (
        sqlgen_target_arg
        "${sqlgen_target_PARAM_OPTIONS}"
        "${sqlgen_target_PARAM_ONE_VALUE_KEYWORDS}"
        "${sqlgen_target_PARAM_MULTI_VALUE_KEYWORDS}"
        ${ARGN})

    if (NOT "${sqlgen_target_arg_UNPARSED_ARGUMENTS}" STREQUAL "")
        message (FATAL_ERROR "sqlgen_target (<name> BACKENDS <sqlite [...]> INPUT <input file> [HEADER file] [SOURCE file])")
    endif ()

    set (_input_file ${sqlgen_target_arg_INPUT})
    set (_output_header ${sqlgen_target_arg_HEADER})
    set (_output_source ${sqlgen_target_arg_SOURCE})

    get_filename_component (_input_name "${_input_file}" NAME)
    if (NOT _output_header)
        set (_output_header "${CMAKE_CURRENT_BINARY_DIR}/${_input_name}.h")
    endif ()
    if (NOT _output_source)
        set (_output_source "${CMAKE_CURRENT_BINARY_DIR}/${_input_name}.c")
    endif ()

    if (NOT IS_ABSOLUTE ${_input_file})
        set (_input_file "${CMAKE_CURRENT_SOURCE_DIR}/${_input_file}")
    endif ()
    if (NOT IS_ABSOLUTE ${_output_header})
        set (_output_header "${CMAKE_CURRENT_BINARY_DIR}/${_output_header}")
    endif ()

    string (REPLACE ";" "," _backends ${sqlgen_target_arg_BACKENDS})

    get_filename_component (_output_path "${_output_header}" DIRECTORY)
    add_custom_command (OUTPUT ${_output_header} ${_output_source}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_output_path}
        COMMAND sqlgen -b ${_backends} -i ${_input_file} --header ${_output_header} --source ${_output_source}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        MAIN_DEPENDENCY ${_input_file}
        DEPENDS sqlgen
        COMMENT "[sqlgen][${name}] Generating SQL bindings for backends: ${_backends}"
        VERBATIM)

    set (SQLGEN_${name}_DEFINED TRUE)
    set (SQLGEN_${name}_OUTPUTS
        ${_output_header}
        ${_output_source})

    unset (_output_path)
    unset (_backends)
    unset (_output_source)
    unset (_input_name)
    unset (_output_header)
    unset (_input_file)
endmacro ()
