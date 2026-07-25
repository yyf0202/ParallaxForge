cmake_minimum_required(VERSION 3.28)
project(ParallaxForgeShaderBuildFixture LANGUAGES NONE)

if(NOT PARALLAX_FORGE_HLSL_MODULE)
  message(FATAL_ERROR "PARALLAX_FORGE_HLSL_MODULE is required.")
endif()
include("${PARALLAX_FORGE_HLSL_MODULE}")

parallax_forge_add_hlsl_library(
  fixture_first
  first/shared.hlsl
  DEPENDS shared.hlsli)
parallax_forge_add_hlsl_library(
  fixture_second
  second/shared.hlsl)
