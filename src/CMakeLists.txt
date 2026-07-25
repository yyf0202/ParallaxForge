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

parallax_forge_add_hlsl_library(
  volume_rasterizer_mark
  voxel/shaders/volume_rasterizer.hlsl
  ENTRY_POINT MarkTriangles
  PROFILE cs_6_2)
parallax_forge_add_hlsl_library(
  volume_rasterizer_dilate
  voxel/shaders/volume_rasterizer.hlsl
  ENTRY_POINT DilateVoxels
  PROFILE cs_6_2)
get_property(_volume_rasterizer_mark_dxil
  TARGET volume_rasterizer_mark PROPERTY PARALLAX_FORGE_DXIL_OUTPUT)
get_property(_volume_rasterizer_dilate_dxil
  TARGET volume_rasterizer_dilate PROPERTY PARALLAX_FORGE_DXIL_OUTPUT)
file(TO_CMAKE_PATH "${_volume_rasterizer_mark_dxil}"
  _volume_rasterizer_mark_dxil)
file(TO_CMAKE_PATH "${_volume_rasterizer_dilate_dxil}"
  _volume_rasterizer_dilate_dxil)

add_library(parallax_forge_voxel STATIC
  voxel/src/voxel_grid.cpp
  voxel/src/volume_rasterizer.cpp)
add_library(parallax_forge::voxel ALIAS parallax_forge_voxel)
add_dependencies(parallax_forge_voxel
  volume_rasterizer_mark
  volume_rasterizer_dilate)
target_include_directories(parallax_forge_voxel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/voxel/include>)
target_compile_features(parallax_forge_voxel PUBLIC cxx_std_20)
target_compile_definitions(parallax_forge_voxel PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  PARALLAX_FORGE_VOLUME_RASTERIZER_MARK_DXIL="${_volume_rasterizer_mark_dxil}"
  PARALLAX_FORGE_VOLUME_RASTERIZER_DILATE_DXIL="${_volume_rasterizer_dilate_dxil}")
target_link_libraries(parallax_forge_voxel
  PUBLIC
    parallax_forge::core
    parallax_forge::gpu)
if(MSVC)
  target_compile_options(parallax_forge_voxel PRIVATE /W4 /permissive-)
  if(PARALLAX_FORGE_WARNINGS_AS_ERRORS)
    target_compile_options(parallax_forge_voxel PRIVATE /WX)
  endif()
endif()
