# GPU PVS Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Build a headless DirectX 12/DXR baker that converts configured OBJ geometry into retained-algorithm probes and six-face PVS output.

**Architecture:** CPU code validates JSON, imports OBJ meshes, owns DX12 resources, and maps dense GPU slots to stable object IDs. HLSL implements retained triangle-AABB marking, integer-sphere dilation, nine-point sampling, and six-face closest-hit bitsets. The batch app owns orchestration and export.

**Tech Stack:** C++20, CMake 3.28, Windows SDK D3D12/DXGI, DXC Shader Model 6.3, DXR 1.0, nlohmann_json, tinyobjloader.

## Global Constraints

- Windows 10/11 and a DXR-capable hardware adapter are the only runtime target.
- All symbols use namespace parallax_forge; no predecessor names, identity, source, data, or assets may enter the public tree.
- Retain world-AABB triangle marking, integer-sphere dilation, 4.0 m blocks, eight delta-inset corners plus centre, always-include volume acceptance, six cube faces, nearest opaque hits, and object bitsets.
- Omit wall polygons, terrain/floor gates, split bounds, material recursion, LOD, and distance policies.
- Defaults: voxel 1.0, dilation radius 5, block 4.0, delta 0.2, face resolution 600, max distance 5000.0.
- Output uses stable object_id only; GPU object slots are internal.
- Inputs and outputs are config-relative; failed bakes cannot replace prior JSON/PFVIS output.
- CI builds and runs CPU tests only. Local DXR smoke validation needs compatible hardware.

---

## Task 1: Retained configuration and neutral OBJ world model

**Files:**
- Modify: cmake/Dependencies.cmake
- Modify: src/CMakeLists.txt
- Modify: src/world/include/parallax_forge/world/types.hpp
- Modify: src/world/include/parallax_forge/world/bake_config.hpp
- Modify: src/world/src/config_loader.cpp
- Create: src/world/include/parallax_forge/world/world_model.hpp
- Create: src/world/include/parallax_forge/world/obj_world_importer.hpp
- Create: src/world/src/obj_world_importer.cpp
- Modify: tests/CMakeLists.txt
- Modify: tests/test_config_loader.cpp
- Create: tests/test_obj_world_importer.cpp
- Modify: assets/demo-chamber/bake.json

**Interfaces:**
- Produces:
~~~cpp
namespace parallax_forge::world {
struct AlwaysIncludeVolume { Transform local_to_world; Transform world_to_local; };
struct VoxelSettings { float size; std::uint32_t dilation_radius; };
struct ProbeSettings {
  float storage_cell_size;
  float delta;
  std::vector<AlwaysIncludeVolume> always_include_volumes;
};
struct Triangle { Vec3 a; Vec3 b; Vec3 c; };
struct ImportedObject {
  ObjectId object_id;
  InstanceSlot slot;
  Transform local_to_world;
  std::vector<Triangle> triangles;
};
struct WorldModel { Bounds bounds; std::vector<ImportedObject> objects; };
[[nodiscard]] WorldModel ImportWorld(const BakeConfig& config);
}
~~~

- [ ] **Step 1: Write failing tests**

  Add these cases to tests/test_config_loader.cpp and tests/test_obj_world_importer.cpp:

~~~cpp
assert(LoadFromText(R"({
  "world":{"bounds":{"min":[0,0,0],"max":[8,8,8]},"objects":[]},
  "voxel":{"size":1.0,"dilation_radius":5},
  "probes":{"storage_cell_size":4.0,"delta":0.2,"always_include_volumes":[]},
  "trace":{"face_resolution":600,"max_distance":5000.0},
  "output":{"directory":"out","write_json":true,"write_binary":true}
})").probes.storage_cell_size == 4.0f);
assert(ThrowsConfiguration("storage_cell_size must equal 4"));
assert(ThrowsConfiguration("dilation_radius must be a positive integer"));
assert(ThrowsConfiguration("always_include_volumes[0].transform must be invertible"));
const auto world = ImportWorld(LoadDemoConfig());
assert(world.objects.size() == 2);
assert(world.objects[0].slot == 0);
assert(!world.objects[0].triangles.empty());
~~~

- [ ] **Step 2: Run tests to verify failure**

Run:
~~~powershell
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug -R "config_loader|obj_world_importer" --output-on-failure
~~~

Expected: importer target is absent and retained JSON keys are rejected.

- [ ] **Step 3: Implement configuration and OBJ import**

Pin tinyobjloader v2.0.0rc13 in cmake/Dependencies.cmake. Extend BakeConfig with the shown settings. Parse a finite 16-float row-major transform for each volume, calculate its finite inverse, and reject a singular transform. Require size > 0, positive integer dilation radius, exact 4.0 block size, 0 <= delta < 2.0, face resolution > 0, and max distance > 0.

Implement ImportWorld using tinyobj::LoadObj once per object, triangulating input, producing position-only Triangle records, and rejecting meshes that yield no triangles. Build dense slots from ObjectRegistry's sorted entries.

- [ ] **Step 4: Verify implementation**

Run:
~~~powershell
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug -R "config_loader|obj_world_importer" --output-on-failure
git diff --check
~~~

Expected: both tests pass and diff check is silent.

- [ ] **Step 5: Commit**

~~~powershell
git add cmake/Dependencies.cmake src/CMakeLists.txt src/world assets/demo-chamber/bake.json tests
git commit -m "feat: import retained-core bake worlds"
~~~

## Task 2: DirectX 12/DXR substrate and shader build

**Files:**
- Modify: CMakeLists.txt
- Modify: src/CMakeLists.txt
- Create: src/gpu/include/parallax_forge/gpu/gpu_context.hpp
- Create: src/gpu/include/parallax_forge/gpu/gpu_buffer.hpp
- Create: src/gpu/include/parallax_forge/gpu/descriptor_arena.hpp
- Create: src/gpu/include/parallax_forge/gpu/shader_path.hpp
- Create: src/gpu/src/gpu_context.cpp
- Create: src/gpu/src/gpu_buffer.cpp
- Create: src/gpu/src/descriptor_arena.cpp
- Create: src/gpu/src/shader_path.cpp
- Create: tests/test_shader_path.cpp
- Modify: tests/CMakeLists.txt

**Interfaces:**
~~~cpp
namespace parallax_forge::gpu {
class GpuContext {
 public:
  static GpuContext Create();
  [[nodiscard]] bool SupportsDxr() const noexcept;
  [[nodiscard]] ID3D12Device5* device() const noexcept;
  [[nodiscard]] ID3D12GraphicsCommandList4* BeginCommands();
  void ExecuteAndWait();
};
class GpuBuffer {
 public:
  static GpuBuffer DefaultUav(GpuContext&, std::uint64_t bytes);
  static GpuBuffer Upload(GpuContext&, std::span<const std::byte>);
  [[nodiscard]] std::uint64_t gpu_address() const noexcept;
};
[[nodiscard]] std::filesystem::path ShaderOutputFile(
  const std::filesystem::path& output_directory,
  const std::filesystem::path& source_file);
}
~~~

- [ ] **Step 1: Write a failing shader-output-path test**

~~~cpp
using parallax_forge::gpu::ShaderOutputFile;
const auto output = ShaderOutputFile("generated/shaders", "src/voxel/shaders/volume_rasterizer.hlsl");
assert(output == std::filesystem::path("generated/shaders/volume_rasterizer.dxil"));
~~~

- [ ] **Step 2: Run test to verify failure**

Run:
~~~powershell
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug -R shader_path --output-on-failure
~~~

Expected: target is absent because ShaderOutputFile is not declared.

- [ ] **Step 3: Implement DX12 substrate without fallback**

Link d3d12, dxgi, and dxguid. CMake discovers dxc.exe via PARALLAX_FORGE_DXC_EXECUTABLE, then find_program, and fails configuration with an explicit cache-variable instruction if none is found. Add a custom command per HLSL library that invokes dxc.exe with target lib_6_3 and a binary DXIL output path.

GpuContext selects hardware only, creates ID3D12Device5, direct queue, fence, event, allocator, and command list. It checks D3D12_FEATURE_D3D12_OPTIONS5 for RaytracingTier >= 1.0 and raises a runtime_error otherwise. GpuBuffer exposes only resource, bytes, GPU address, upload copy, and readback copy required by following tasks. Do not create WARP or a non-DXR code path.

- [ ] **Step 4: Verify implementation**

Run:
~~~powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug -R shader_path --output-on-failure
~~~

Expected: configuration finds local DXC; the CPU-only shader path test passes.

- [ ] **Step 5: Commit**

~~~powershell
git add CMakeLists.txt src tests
git commit -m "feat: add DirectX 12 DXR substrate"
~~~

## Task 3: Retained GPU voxel marking and spherical dilation

**Files:**
- Create: src/voxel/include/parallax_forge/voxel/voxel_grid.hpp
- Create: src/voxel/include/parallax_forge/voxel/volume_rasterizer.hpp
- Create: src/voxel/src/voxel_grid.cpp
- Create: src/voxel/src/volume_rasterizer.cpp
- Create: src/voxel/shaders/volume_rasterizer.hlsl
- Modify: src/CMakeLists.txt
- Create: tests/test_voxel_grid.cpp
- Modify: tests/CMakeLists.txt

**Interfaces:**
~~~cpp
namespace parallax_forge::voxel {
struct GridShape { std::uint32_t x, y, z; Vec3 origin; float voxel_size; };
struct GpuVoxelField { GridShape grid; gpu::GpuBuffer final_field; };
[[nodiscard]] GridShape MakeGrid(const Bounds&, float voxel_size);
[[nodiscard]] bool InDilationSphere(int dx, int dy, int dz, std::uint32_t radius);
class VolumeRasterizer {
 public:
  explicit VolumeRasterizer(gpu::GpuContext&);
  [[nodiscard]] GpuVoxelField Rasterize(const world::WorldModel&, const world::VoxelSettings&);
};
}
~~~

- [ ] **Step 1: Write failing retained-math test**

~~~cpp
const Bounds bounds{{0, 0, 0}, {8.1f, 4.0f, 4.0f}};
const auto grid = MakeGrid(bounds, 1.0f);
assert(grid.x == 9 && grid.y == 4 && grid.z == 4);
assert(InDilationSphere(3, 4, 0, 5));
assert(!InDilationSphere(4, 4, 0, 5));
~~~

- [ ] **Step 2: Run test to verify failure**

Run:
~~~powershell
ctest --test-dir build/windows-debug -C Debug -R voxel_grid --output-on-failure
~~~

Expected: target is absent.

- [ ] **Step 3: Implement exact retained kernels**

MakeGrid uses ceil per axis and bounds.min as origin. HLSL MarkTriangles has one thread per triangle, transforms all vertices, computes min/max, clamps integer voxel coordinates, and loops inclusively through x/y/z to set the temporary uint field to 1. HLSL DilateVoxels has one thread per final voxel and checks temporary marked voxels only when squared integer distance is within radius squared.

Allocate two default-heap UAV uint buffers and place UAV barriers between clear, marking, and dilation. Do not substitute triangle intersection or box-shaped dilation.

- [ ] **Step 4: Verify implementation**

Run:
~~~powershell
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug -R voxel_grid --output-on-failure
git diff --check
~~~

Expected: retained-math test passes and the HLSL command compiles.

- [ ] **Step 5: Commit**

~~~powershell
git add src/voxel src/CMakeLists.txt tests
git commit -m "feat: add retained GPU voxel rasterizer"
~~~

## Task 4: Retained 4 m / nine-point GPU probe generation

**Files:**
- Create: src/sampling/include/parallax_forge/sampling/probe_math.hpp
- Create: src/sampling/include/parallax_forge/sampling/probe_generator.hpp
- Create: src/sampling/src/probe_math.cpp
- Create: src/sampling/src/probe_generator.cpp
- Create: src/sampling/shaders/probe_generator.hlsl
- Modify: src/CMakeLists.txt
- Create: tests/test_probe_math.cpp
- Modify: tests/CMakeLists.txt

**Interfaces:**
~~~cpp
namespace parallax_forge::sampling {
[[nodiscard]] std::array<Vec3, 9> CandidatePoints(Vec3 block_min, float delta);
[[nodiscard]] bool AcceptCandidate(bool dilated_voxel, bool inside_volume);
class ProbeGenerator {
 public:
  explicit ProbeGenerator(gpu::GpuContext&);
  [[nodiscard]] std::vector<Vec3> Generate(
    const voxel::GpuVoxelField&, const Bounds&, const world::ProbeSettings&);
};
}
~~~

- [ ] **Step 1: Write failing candidate-order test**

~~~cpp
const auto points = CandidatePoints({0, 0, 0}, 0.2f);
assert(points[0] == Vec3{0.2f, 0.2f, 0.2f});
assert(points[7] == Vec3{3.8f, 3.8f, 3.8f});
assert(points[8] == Vec3{2.0f, 2.0f, 2.0f});
assert(AcceptCandidate(true, false));
assert(AcceptCandidate(false, true));
assert(!AcceptCandidate(false, false));
~~~

- [ ] **Step 2: Run test to verify failure**

Run:
~~~powershell
ctest --test-dir build/windows-debug -C Debug -R probe_math --output-on-failure
~~~

Expected: target is absent.

- [ ] **Step 3: Implement append generation**

The CPU calculates block dimensions by truncating extent / 4.0 and allocates append capacity block_x * block_y * block_z * 9, rejecting overflow. HLSL emits candidates 0 through 7 in binary corner order with inward signs; candidate 8 is the centre. It accepts QueryVoxel(final field, point) OR any transformed volume containment test. It appends float3 only. No wall, terrain, split, or policy buffers exist.

Read back append counter and written float3 range. Treat capacity exhaustion as a bake error.

- [ ] **Step 4: Verify implementation**

Run:
~~~powershell
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug -R probe_math --output-on-failure
~~~

Expected: exact point order and all three acceptance states pass.

- [ ] **Step 5: Commit**

~~~powershell
git add src/sampling src/CMakeLists.txt tests
git commit -m "feat: generate retained GPU probes"
~~~

## Task 5: Six-face DXR trace and stable visibility decoding

**Files:**
- Create: src/tracing/include/parallax_forge/tracing/cube_faces.hpp
- Create: src/tracing/include/parallax_forge/tracing/visibility_bitset.hpp
- Create: src/tracing/include/parallax_forge/tracing/dxr_trace_program.hpp
- Create: src/tracing/include/parallax_forge/tracing/visibility_bake_engine.hpp
- Create: src/tracing/src/cube_faces.cpp
- Create: src/tracing/src/visibility_bitset.cpp
- Create: src/tracing/src/dxr_trace_program.cpp
- Create: src/tracing/src/visibility_bake_engine.cpp
- Create: src/tracing/shaders/pvs_trace.hlsl
- Modify: src/CMakeLists.txt
- Create: tests/test_visibility_bitset.cpp
- Modify: tests/CMakeLists.txt

**Interfaces:**
~~~cpp
namespace parallax_forge::tracing {
struct CubeFaceDirection { Vec3 origin, extend_u, extend_v, normal; };
[[nodiscard]] std::array<CubeFaceDirection, 6> CubeFaceDirections();
[[nodiscard]] std::vector<ObjectId> DecodeVisibilityWord(
  std::span<const std::uint32_t> words,
  std::uint32_t local_probe,
  const world::ObjectRegistry&);
class VisibilityBakeEngine {
 public:
  explicit VisibilityBakeEngine(gpu::GpuContext&);
  [[nodiscard]] export_data::VisibilityCatalog Bake(
    const world::WorldModel&, std::span<const Vec3> probes, const world::TraceSettings&);
};
}
~~~

- [ ] **Step 1: Write failing face and bitset tests**

~~~cpp
const std::vector<std::uint32_t> words{0b10u, 0b00u, 0b10u};
const auto ids = DecodeVisibilityWord(words, 1, registry_with_slots_10_20_30);
assert((ids == std::vector<ObjectId>{10, 30}));
const auto faces = CubeFaceDirections();
assert(faces[0].normal == Vec3{0, -1, 0});
assert(faces[1].normal == Vec3{0, 1, 0});
assert(faces[2].normal == Vec3{1, 0, 0});
~~~

- [ ] **Step 2: Run tests to verify failure**

Run:
~~~powershell
ctest --test-dir build/windows-debug -C Debug -R "cube_faces|visibility_bitset" --output-on-failure
~~~

Expected: visibility_bitset target is absent.

- [ ] **Step 3: Implement retained DXR dispatch**

Build BLAS per unique mesh and a TLAS instance per object. TLAS InstanceID equals ObjectRegistry slot. Build one ray-generation, miss, and opaque closest-hit group. Upload eighteen float4 constants: six origins, six U extents, six V extents. Dispatch R*R by 6 by current probe batch.

Ray generation maps x to pixel-centre u/v, normalizes the selected face vector, and traces from the selected probe. Closest-hit obtains InstanceID, calculates local_probe / 32 and local_probe % 32, then InterlockedOrs the correct object word. Clear object_count * ceil(batch_probe_count / 32) words before dispatch. Read words, decode slots, sort stable IDs, and add probes to VisibilityCatalog.

- [ ] **Step 4: Verify implementation**

Run:
~~~powershell
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug -R "cube_faces|visibility_bitset" --output-on-failure
git diff --check
~~~

Expected: face order and bitset decode pass; DXR HLSL compiles.

- [ ] **Step 5: Commit**

~~~powershell
git add src/tracing src/CMakeLists.txt tests
git commit -m "feat: add six-face DXR visibility baking"
~~~

## Task 6: Headless bake, demo, and DXR smoke validation

**Files:**
- Modify: apps/forge-bake/main.cpp
- Modify: apps/forge-bake/CMakeLists.txt
- Modify: assets/demo-chamber/bake.json
- Modify: README.md
- Create: scripts/smoke_dxr_bake.ps1
- Create: tests/test_bake_cli.ps1
- Modify: tests/CMakeLists.txt

**Interfaces:**
- Commands:
~~~text
parallax-forge-bake --config <bake.json> --validate
parallax-forge-bake --config <bake.json> --bake
~~~

- [ ] **Step 1: Write failing non-GPU CLI contract test**

Create tests/test_bake_cli.ps1:

~~~powershell
& $BakeExe --config $DemoConfig --validate
if ($LASTEXITCODE -ne 0) { throw "validate command failed" }
& $BakeExe --config $DemoConfig --bake
if ($LASTEXITCODE -eq 0 -and -not (Test-Path $ExpectedJson)) {
  throw "successful bake did not create JSON"
}
~~~

Only register the validate portion with CTest. The bake command is hardware smoke validation.

- [ ] **Step 2: Run test to verify failure**

Run:
~~~powershell
ctest --test-dir build/windows-debug -C Debug -R bake_cli --output-on-failure
~~~

Expected: target is absent.

- [ ] **Step 3: Implement orchestration and smoke script**

Keep --validate meaning unchanged. --bake creates GpuContext, rejects unavailable DXR before output, imports WorldModel, rasterizes, generates probes, fails when no probes exist, traces VisibilityCatalog, and calls both existing writers per output settings.

The smoke script resolves the configured output directory, verifies it lies below its supplied working root before deletion, runs --bake, decodes JSON/PFVIS with the project reader, compares ordered objects/probes/visible IDs, and prints counts. README documents prerequisites, commands, and that smoke results require hardware.

- [ ] **Step 4: Verify final behaviour**

Run:
~~~powershell
cmake --preset windows-debug
cmake --build build/windows-debug --config Debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
powershell -NoProfile -File scripts/audit_public_tree.ps1 -Root (Get-Location).Path
powershell -NoProfile -File scripts/smoke_dxr_bake.ps1 -BakeExe build/windows-debug/apps/forge-bake/Debug/parallax-forge-bake.exe -Config assets/demo-chamber/bake.json
~~~

Expected: CPU tests and public audit pass. On a DXR adapter the smoke script writes both outputs and reports matching decoded content. Without DXR it returns the explicit support error and is not a success.

- [ ] **Step 5: Commit**

~~~powershell
git add apps assets README.md scripts tests
git commit -m "feat: run end-to-end GPU PVS bakes"
~~~

## Plan self-review

- Spec coverage: Tasks 1-4 implement retained config, AABB marking, spherical dilation, 4 m truncation, nine candidates, delta, and volumes. Task 5 implements R^2 x 6 x batch nearest-hit bitsets and stable IDs. Task 6 composes output and smoke validation.
- Placeholder scan: no undefined implementation references or deferred-work markers remain.
- Type consistency: later tasks consume WorldModel, GpuContext, GpuVoxelField, ProbeGenerator, ObjectRegistry, and VisibilityCatalog defined by earlier tasks.
