# ParallaxForge

ParallaxForge is a headless Windows x64 DirectX Raytracing baker that converts
configured OBJ geometry into probe visibility data.

## Requirements

- Windows 10 version 1809 or later, x64
- Visual Studio 2022 with the C++ desktop development workload
- CMake 3.28 or later
- Windows SDK with the DirectX Shader Compiler (`dxc.exe`)
- A Direct3D 12 adapter with DXR tier 1.0 support for baking

## Configure, build, and test

From the repository root:

```powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

## Validate or bake

Configuration validation does not run GPU work:

```powershell
build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe --config assets/demo-chamber/bake.json --validate
```

Run the complete OBJ, voxel, probe, DXR, and export pipeline with:

```powershell
build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe --config assets/demo-chamber/bake.json --bake
```

The demo writes transactional `visibility.json` and `visibility.pfvis` files
under `assets/demo-chamber/output`.

To clear the generated demo output safely, bake it, and compare the decoded
JSON/PFVIS catalogs:

```powershell
powershell -NoProfile -File scripts/smoke_dxr_bake.ps1 -BakeExe build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe -Config assets/demo-chamber/bake.json
```

The smoke result is hardware evidence and succeeds only on a DXR-capable
adapter. The regular CTest CLI contract performs validation only.

## Demo asset

`assets/demo-chamber` is a self-authored chamber scene used to exercise the
configured voxel dilation, 4 m nine-point probe sampling, an always-include
volume, six-face visibility tracing, and both output formats.
