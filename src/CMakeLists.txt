add_library(parallax_forge_core STATIC core/src/version.cpp)
add_library(parallax_forge::core ALIAS parallax_forge_core)
target_sources(parallax_forge_core PRIVATE world/src/object_registry.cpp)
target_sources(parallax_forge_core PRIVATE world/src/config_loader.cpp)
target_sources(parallax_forge_core PRIVATE world/src/obj_world_importer.cpp)
target_sources(parallax_forge_core PRIVATE export/src/visibility_catalog.cpp)
target_sources(parallax_forge_core PRIVATE export/src/json_visibility_writer.cpp)
target_sources(parallax_forge_core PRIVATE export/src/pfvis_codec.cpp)
target_sources(parallax_forge_core PRIVATE export/src/transactional_output.cpp)
target_sources(parallax_forge_core PRIVATE export/src/visibility_output_pair.cpp)
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

parallax_forge_add_hlsl_library(
  probe_generator_shader
  sampling/shaders/probe_generator.hlsl
  ENTRY_POINT GenerateProbes
  PROFILE cs_6_2)
get_property(_probe_generator_dxil
  TARGET probe_generator_shader PROPERTY PARALLAX_FORGE_DXIL_OUTPUT)
file(TO_CMAKE_PATH "${_probe_generator_dxil}" _probe_generator_dxil)

add_library(parallax_forge_sampling STATIC
  sampling/src/probe_math.cpp
  sampling/src/probe_generator.cpp)
add_library(parallax_forge::sampling ALIAS parallax_forge_sampling)
add_dependencies(parallax_forge_sampling probe_generator_shader)
target_include_directories(parallax_forge_sampling PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sampling/include>)
target_compile_features(parallax_forge_sampling PUBLIC cxx_std_20)
target_compile_definitions(parallax_forge_sampling PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  PARALLAX_FORGE_PROBE_GENERATOR_DXIL="${_probe_generator_dxil}")
target_link_libraries(parallax_forge_sampling
  PUBLIC
    parallax_forge::voxel)
if(MSVC)
  target_compile_options(parallax_forge_sampling PRIVATE /W4 /permissive-)
  if(PARALLAX_FORGE_WARNINGS_AS_ERRORS)
    target_compile_options(parallax_forge_sampling PRIVATE /WX)
  endif()
endif()

parallax_forge_add_hlsl_library(
  pvs_trace_shader
  tracing/shaders/pvs_trace.hlsl
  PROFILE lib_6_3)
get_property(_pvs_trace_dxil
  TARGET pvs_trace_shader PROPERTY PARALLAX_FORGE_DXIL_OUTPUT)
file(TO_CMAKE_PATH "${_pvs_trace_dxil}" _pvs_trace_dxil)

add_library(parallax_forge_tracing STATIC
  tracing/src/cube_faces.cpp
  tracing/src/dxr_trace_program.cpp
  tracing/src/visibility_bake_engine.cpp
  tracing/src/visibility_bitset.cpp)
add_library(parallax_forge::tracing ALIAS parallax_forge_tracing)
add_dependencies(parallax_forge_tracing pvs_trace_shader)
target_include_directories(parallax_forge_tracing PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tracing/include>)
target_compile_features(parallax_forge_tracing PUBLIC cxx_std_20)
target_compile_definitions(parallax_forge_tracing PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  PARALLAX_FORGE_PVS_TRACE_DXIL="${_pvs_trace_dxil}")
target_link_libraries(parallax_forge_tracing
  PUBLIC
    parallax_forge::core
    parallax_forge::gpu)
if(MSVC)
  target_compile_options(parallax_forge_tracing PRIVATE /W4 /permissive-)
  if(PARALLAX_FORGE_WARNINGS_AS_ERRORS)
    target_compile_options(parallax_forge_tracing PRIVATE /WX)
  endif()
endif()
