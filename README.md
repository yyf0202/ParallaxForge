# ParallaxForge

ParallaxForge is a Windows DirectX 12 PVS baker: it turns OBJ scene geometry
into per-Probe visibility data.

## Core

- **GPU voxel occupancy** - marks world-space triangle AABBs and applies
  spherical dilation.
- **Free-voxel Probes** - emits the eight delta-inset corners and center of
  each 4 m storage cell.
- **DXR visibility baking** - traces six cube faces per Probe, keeps the
  nearest opaque hit, and emits stable object IDs.

## Build

Requires Windows x64, Visual Studio 2022, CMake 3.28+, the Windows SDK with
`dxc.exe`, and a DXR tier 1.0 adapter for baking.

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
