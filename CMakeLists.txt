cmake_minimum_required(VERSION 3.28)
project(ParallaxForge VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "ParallaxForge requires Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(PARALLAX_FORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

include(CTest)
include(cmake/Dependencies.cmake)
add_subdirectory(src)
add_subdirectory(apps/forge-bake)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
