add_executable(parallax_forge_test_version test_version.cpp)
target_link_libraries(parallax_forge_test_version PRIVATE parallax_forge::core)
add_test(NAME parallax_forge_test_version COMMAND parallax_forge_test_version)

add_executable(parallax_forge_test_object_registry test_object_registry.cpp)
target_link_libraries(parallax_forge_test_object_registry PRIVATE parallax_forge::core)
add_test(NAME parallax_forge_test_object_registry COMMAND parallax_forge_test_object_registry)

add_executable(parallax_forge_test_config_loader test_config_loader.cpp)
target_link_libraries(parallax_forge_test_config_loader PRIVATE parallax_forge::core)
add_test(NAME parallax_forge_test_config_loader COMMAND parallax_forge_test_config_loader)

add_executable(parallax_forge_test_obj_world_importer test_obj_world_importer.cpp)
target_link_libraries(parallax_forge_test_obj_world_importer PRIVATE parallax_forge::core)
add_test(NAME parallax_forge_test_obj_world_importer COMMAND parallax_forge_test_obj_world_importer)

add_executable(parallax_forge_test_json_visibility_writer test_json_visibility_writer.cpp)
target_link_libraries(parallax_forge_test_json_visibility_writer PRIVATE parallax_forge::core)
add_test(NAME parallax_forge_test_json_visibility_writer COMMAND parallax_forge_test_json_visibility_writer)

add_executable(parallax_forge_test_pfvis_codec test_pfvis_codec.cpp)
target_link_libraries(parallax_forge_test_pfvis_codec PRIVATE parallax_forge::core)
add_test(NAME parallax_forge_test_pfvis_codec COMMAND parallax_forge_test_pfvis_codec)

add_executable(parallax_forge_test_shader_path test_shader_path.cpp)
target_link_libraries(parallax_forge_test_shader_path PRIVATE parallax_forge::gpu)
add_test(NAME parallax_forge_test_shader_path COMMAND parallax_forge_test_shader_path)

add_executable(parallax_forge_test_gpu_buffer test_gpu_buffer.cpp)
target_link_libraries(parallax_forge_test_gpu_buffer PRIVATE parallax_forge::gpu)
add_test(NAME parallax_forge_test_gpu_buffer COMMAND parallax_forge_test_gpu_buffer)
set_tests_properties(parallax_forge_test_gpu_buffer
  PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME parallax_forge_test_shader_build
  COMMAND powershell -NoProfile -File ${CMAKE_CURRENT_SOURCE_DIR}/test_shader_build.ps1
    -FixtureRoot ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/shader_build
    -BinaryRoot ${CMAKE_CURRENT_BINARY_DIR}/shader-build-integration
    -HlslModule ${CMAKE_SOURCE_DIR}/cmake/CompileHlsl.cmake
    -DxcExecutable ${PARALLAX_FORGE_DXC_EXECUTABLE}
    -Generator "${CMAKE_GENERATOR}"
    -Architecture "${CMAKE_GENERATOR_PLATFORM}")

add_test(NAME parallax_forge_test_dxc_validation
  COMMAND powershell -NoProfile -File ${CMAKE_CURRENT_SOURCE_DIR}/test_dxc_validation.ps1
    -SourceRoot ${CMAKE_SOURCE_DIR}
    -BinaryRoot ${CMAKE_CURRENT_BINARY_DIR}/dxc-validation
    -Generator "${CMAKE_GENERATOR}"
    -Architecture "${CMAKE_GENERATOR_PLATFORM}")

add_test(NAME parallax_forge_test_forge_bake_validate
  COMMAND $<TARGET_FILE:parallax-forge-bake>
    --config ${CMAKE_SOURCE_DIR}/assets/demo-chamber/bake.json --validate)

add_test(NAME parallax_forge_test_public_tree_audit
  COMMAND powershell -NoProfile -File ${CMAKE_CURRENT_SOURCE_DIR}/test_audit_public_tree.ps1
    -AuditScript ${CMAKE_SOURCE_DIR}/scripts/audit_public_tree.ps1)

add_test(NAME parallax_forge_test_warnings_as_errors
  COMMAND powershell -NoProfile -File ${CMAKE_CURRENT_SOURCE_DIR}/test_warnings_as_errors.ps1
    -SourceRoot ${CMAKE_SOURCE_DIR}
    -BinaryRoot ${CMAKE_BINARY_DIR}/warnings-as-errors-test)
