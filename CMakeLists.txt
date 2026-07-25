cmake_minimum_required(VERSION 3.28)
project(ParallaxForge VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "ParallaxForge requires Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(PARALLAX_FORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

if(NOT PARALLAX_FORGE_DXC_EXECUTABLE)
  file(GLOB _parallax_forge_windows_sdk_bin_directories
    LIST_DIRECTORIES true
    "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64")
  list(SORT _parallax_forge_windows_sdk_bin_directories
    COMPARE NATURAL ORDER DESCENDING)
  unset(PARALLAX_FORGE_DXC_EXECUTABLE CACHE)
  find_program(PARALLAX_FORGE_DXC_EXECUTABLE
    NAMES dxc.exe dxc
    HINTS ${_parallax_forge_windows_sdk_bin_directories}
    DOC "Full path to the DirectX Shader Compiler executable")
endif()

if(NOT PARALLAX_FORGE_DXC_EXECUTABLE
    OR NOT EXISTS "${PARALLAX_FORGE_DXC_EXECUTABLE}"
    OR IS_DIRECTORY "${PARALLAX_FORGE_DXC_EXECUTABLE}")
  message(FATAL_ERROR
    "PARALLAX_FORGE_DXC_EXECUTABLE must name a working "
    "DirectX Shader Compiler executable. "
    "Set it to the full path to dxc.exe.")
endif()
execute_process(
  COMMAND "${PARALLAX_FORGE_DXC_EXECUTABLE}" -help
  RESULT_VARIABLE _parallax_forge_dxc_result
  OUTPUT_VARIABLE _parallax_forge_dxc_output
  ERROR_VARIABLE _parallax_forge_dxc_error)
set(_parallax_forge_dxc_help
  "${_parallax_forge_dxc_output}\n${_parallax_forge_dxc_error}")
if(NOT _parallax_forge_dxc_result EQUAL 0
    OR NOT _parallax_forge_dxc_help MATCHES
      "OVERVIEW:[ \t]+HLSL Compiler"
    OR NOT _parallax_forge_dxc_help MATCHES
      "USAGE:[ \t]+dxc[.]exe")
  message(FATAL_ERROR
    "PARALLAX_FORGE_DXC_EXECUTABLE must name a working "
    "DirectX Shader Compiler executable. "
    "Set it to the full path to dxc.exe.")
endif()
set(PARALLAX_FORGE_DXC_EXECUTABLE "${PARALLAX_FORGE_DXC_EXECUTABLE}"
  CACHE FILEPATH "Full path to the DirectX Shader Compiler executable" FORCE)
message(STATUS "Using DirectX Shader Compiler: ${PARALLAX_FORGE_DXC_EXECUTABLE}")

include(CTest)
include(cmake/Dependencies.cmake)
include(cmake/CompileHlsl.cmake)
add_subdirectory(src)
add_subdirectory(apps/forge-bake)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
