add_library(parallax_forge_core STATIC core/src/version.cpp)
add_library(parallax_forge::core ALIAS parallax_forge_core)
target_sources(parallax_forge_core PRIVATE world/src/object_registry.cpp)
target_sources(parallax_forge_core PRIVATE world/src/config_loader.cpp)
target_sources(parallax_forge_core PRIVATE world/src/obj_world_importer.cpp)
target_sources(parallax_forge_core PRIVATE export/src/visibility_catalog.cpp)
target_sources(parallax_forge_core PRIVATE export/src/json_visibility_writer.cpp)
target_sources(parallax_forge_core PRIVATE export/src/pfvis_codec.cpp)
target_sources(parallax_forge_core PRIVATE export/src/transactional_output.cpp)
target_include_directories(parallax_forge_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/world/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/export/include>)
target_compile_features(parallax_forge_core PUBLIC cxx_std_20)
target_link_libraries(parallax_forge_core
  PUBLIC nlohmann_json::nlohmann_json
  PRIVATE tinyobjloader)
if(MSVC)
  target_compile_options(parallax_forge_core PRIVATE /W4 /permissive-)
  if(PARALLAX_FORGE_WARNINGS_AS_ERRORS)
    target_compile_options(parallax_forge_core PRIVATE /WX)
  endif()
endif()

add_library(parallax_forge_gpu STATIC
  gpu/src/descriptor_arena.cpp
  gpu/src/gpu_buffer.cpp
  gpu/src/gpu_context.cpp
  gpu/src/shader_path.cpp)
add_library(parallax_forge::gpu ALIAS parallax_forge_gpu)
target_include_directories(parallax_forge_gpu PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gpu/include>)
target_compile_features(parallax_forge_gpu PUBLIC cxx_std_20)
target_compile_definitions(parallax_forge_gpu PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN)
target_link_libraries(parallax_forge_gpu PRIVATE d3d12 dxgi dxguid)
if(MSVC)
  target_compile_options(parallax_forge_gpu PRIVATE /W4 /permissive-)
  if(PARALLAX_FORGE_WARNINGS_AS_ERRORS)
    target_compile_options(parallax_forge_gpu PRIVATE /WX)
  endif()
endif()

function(parallax_forge_add_hlsl_library target source_file)
  get_filename_component(_source_file "${source_file}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(_shader_name "${_source_file}" NAME_WE)
  set(_output_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  set(_output_file "${_output_directory}/${_shader_name}.dxil")

  add_custom_command(
    OUTPUT "${_output_file}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_directory}"
    COMMAND "${PARALLAX_FORGE_DXC_EXECUTABLE}"
      -T lib_6_3
      -Fo "${_output_file}"
      "${_source_file}"
    DEPENDS "${_source_file}"
    COMMENT "Compiling ${_shader_name}.hlsl to DXIL"
    VERBATIM)
  add_custom_target("${target}" DEPENDS "${_output_file}")
  set_property(TARGET "${target}"
    PROPERTY PARALLAX_FORGE_DXIL_OUTPUT "${_output_file}")
endfunction()
