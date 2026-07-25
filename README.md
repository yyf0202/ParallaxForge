# ParallaxForge

ParallaxForge is a Windows DirectX 12 PVS baker: it turns OBJ scene geometry
into per-Probe visibility data.

## Core

- **GPU voxel occupancy**
- **Free-voxel Probe generation**
- **Six-face DXR visibility baking**

## Build

Requires Windows x64, Visual Studio 2022, CMake 3.28+, the Windows SDK with
`dxc.exe`, and a DirectX 12 adapter with DXR tier 1.0 support for baking.

```powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
```

## Bake the demo

```powershell
build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe --config assets/demo-chamber/bake.json --bake
```

The bake writes `visibility.json` and `visibility.pfvis` under
`assets/demo-chamber/output`.
