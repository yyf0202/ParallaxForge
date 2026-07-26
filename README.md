# ParallaxForge

[中文](README.zh-CN.md)

**ParallaxForge is a C++ and DirectX Raytracing (DXR) PVS (Potentially Visible
Set) baker.**

Compared with conventional rasterized PVS baking workflows, ParallaxForge
delivers roughly **4–10×** faster bake times in benchmark scenes. Actual gains
depend on scene complexity, Probe density, and GPU performance.

For static scenes, the baker traces from each Probe in six orthogonal
directions to cover the full environment, producing the visible-object set for
each Probe and runtime-ready PVS data.

## Geometry-Voxelized Probe Generator

ParallaxForge's key enabling technology is a GPU geometry-voxelized Probe
generator.

It converts OBJ scene geometry into occupancy voxels, dilates occupied cells,
and generates Probes in valid free space. The resulting samples follow the
scene's usable space instead of a simple regular grid.

## DXR PVS Baker

For each Probe, the baker traces DXR rays across six orthogonal directions,
records the nearest opaque hit, and merges hits into a GPU visible-object set.

The bake writes:

- `visibility.json` — human-readable output for inspection and tooling.
- `visibility.pfvis` — compact binary PVS data for runtime use.

## Build

Requires Windows x64, Visual Studio 2022, CMake 3.28+, the Windows SDK with
`dxc.exe`, and a DirectX 12 adapter with DXR tier 1.0 support for baking.

```powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
```

## Bake the Demo

```powershell
build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe --config assets/demo-chamber/bake.json --bake
```
