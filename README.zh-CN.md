# ParallaxForge

[English](README.md)

**ParallaxForge 是一个基于 C++ 和 DirectX Raytracing（DXR）的光线追踪
PVS（Potentially Visible Set，潜在可见集）烘焙器。**

相比传统的光栅化 PVS 烘焙流程，ParallaxForge 在基准场景中可实现约
**4–10 倍**的烘焙效率提升；实际收益取决于场景复杂度、Probe 密度与显卡性能。

它面向静态场景：从每个 Probe 出发，以六个正交方向覆盖完整环境，计算该 Probe
可见的对象集合，并输出可供运行时加载的 PVS 数据。

## 基于几何体素化的 Probe 生成器

ParallaxForge 的关键创新是基于场景几何 GPU 体素化的 Probe 生成器。

它将 OBJ 场景几何转换为体素占据数据，对占据体素执行膨胀，并在有效自由空间内
生成 Probe。这样得到的采样点来自场景实际可活动空间，而不是简单的规则网格。

## DXR PVS 烘焙器

对每个 Probe，烘焙器使用 DXR 在六个正交方向上追踪光线，记录最近不透明命中，
并在 GPU 上归并为可见对象集合。

烘焙输出：

- `visibility.json`：便于检查和工具接入的可读结果。
- `visibility.pfvis`：紧凑的运行时二进制 PVS 数据。

## 构建

需要 Windows x64、Visual Studio 2022、CMake 3.28+、包含 `dxc.exe` 的
Windows SDK，以及支持 DXR tier 1.0 的 DirectX 12 显卡。

```powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
```

## 烘焙示例场景

```powershell
build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe --config assets/demo-chamber/bake.json --bake
```
