# ParallaxForge

ParallaxForge is a Windows x64 foundation for validating scene bake inputs and
writing visibility data contracts.

## Requirements

- Windows 10 or later, x64
- Visual Studio 2022 with the C++ desktop development workload
- CMake 3.28 or later

## Configure, build, and test

From the repository root:

```powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

## Validate a bake configuration

`parallax-forge-bake` currently validates a bake configuration before any bake
work starts:

```powershell
build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe --config assets/demo-chamber/bake.json --validate
```

## Demo asset

`assets/demo-chamber` is a self-authored chamber scene used to exercise the
configuration and visibility-data pipeline. Its `bake.json` describes the
scene and its mesh files are kept alongside it for a reproducible example.

## Current scope

This foundation validates and serializes bake inputs. GPU baking arrives in the
next implementation plan.
