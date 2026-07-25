include_guard(GLOBAL)

function(parallax_forge_add_hlsl_library target source_file)
  cmake_parse_arguments(PARSE_ARGV 2 _hlsl "" "" "DEPENDS")
  if(_hlsl_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "Unknown arguments for ${target}: ${_hlsl_UNPARSED_ARGUMENTS}")
  endif()

  get_filename_component(_source_file "${source_file}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(_dependencies)
  foreach(_dependency IN LISTS _hlsl_DEPENDS)
    get_filename_component(_absolute_dependency "${_dependency}" ABSOLUTE
      BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND _dependencies "${_absolute_dependency}")
  endforeach()

  set(_output_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  set(_output_file "${_output_directory}/${target}.dxil")

  add_custom_command(
    OUTPUT "${_output_file}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_directory}"
    COMMAND "${PARALLAX_FORGE_DXC_EXECUTABLE}"
      -T lib_6_3
      -Fo "${_output_file}"
      "${_source_file}"
    DEPENDS "${_source_file}" ${_dependencies}
    COMMENT "Compiling HLSL library ${target}"
    VERBATIM)
  add_custom_target("${target}" ALL DEPENDS "${_output_file}")
  set_property(TARGET "${target}"
    PROPERTY PARALLAX_FORGE_DXIL_OUTPUT "${_output_file}")
endfunction()
