include_guard(GLOBAL)

function(parallax_forge_add_hlsl_library target source_file)
  cmake_parse_arguments(PARSE_ARGV 2 _hlsl "" "ENTRY_POINT;PROFILE" "DEPENDS")
  if(_hlsl_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "Unknown arguments for ${target}: ${_hlsl_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT _hlsl_PROFILE)
    set(_hlsl_PROFILE lib_6_3)
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
  set(_entry_point_arguments)
  if(_hlsl_ENTRY_POINT)
    list(APPEND _entry_point_arguments -E "${_hlsl_ENTRY_POINT}")
  endif()

  add_custom_command(
    OUTPUT "${_output_file}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_directory}"
    COMMAND "${PARALLAX_FORGE_DXC_EXECUTABLE}"
      -T "${_hlsl_PROFILE}"
      ${_entry_point_arguments}
      -Fo "${_output_file}"
      "${_source_file}"
    DEPENDS "${_source_file}" ${_dependencies}
    COMMENT "Compiling HLSL target ${target}"
    VERBATIM)
  add_custom_target("${target}" ALL DEPENDS "${_output_file}")
  set_property(TARGET "${target}"
    PROPERTY PARALLAX_FORGE_DXIL_OUTPUT "${_output_file}")
endfunction()
