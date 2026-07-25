# ParallaxForge GPU PVS Core Design

**Status:** Approved for implementation
**Target:** Windows 10/11, DirectX 12, DXR-capable GPU
**Scope:** GPU baking core. Viewer work follows this milestone.

## Goal

Implement a clean standalone DirectX 12/DXR baking path that preserves the retained
reference algorithm while using new modules, names, data contracts, and assets.

~~~
OBJ meshes
  -> GPU triangle world-AABB voxel marking
  -> GPU spherical voxel dilation
  -> GPU 4 m storage-block candidate generation
  -> GPU always-include-volume acceptance
  -> append buffer of probes
  -> six-face DXR visibility dispatch
  -> GPU per-probe/object bitset
  -> stable object IDs
  -> JSON and PFVIS
~~~

This is a behaviour-preserving reimplementation. It does not copy source, names,
data, or non-core rules from the predecessor.

## Retained sampling algorithm

### Voxel field

The configured world bounds create a dense voxel volume. Per-axis resolution is:

~~~
ceil(world_extent / voxel_size)
~~~

For every OBJ triangle, the GPU transforms vertices into world space, calculates the
triangle world-space AABB, converts it to a clamped voxel range, and marks every voxel
in that range. This intentionally remains AABB voxel marking; it is not an exact
triangle/voxel intersection algorithm.

The temporary field is dilated into the final field. A final voxel is marked if any
temporary marked voxel is within the integer Euclidean sphere:

~~~
dx*dx + dy*dy + dz*dz <= dilation_radius*dilation_radius
~~~

Default retained values are voxel_size 1.0 and dilation_radius 5 voxels.

### Probe candidates

The world is traversed in fixed 4.0 m storage blocks. It uses the reference integer
truncation rule: partial trailing blocks are not traversed.

Each block emits nine candidates in exact order:

1. Eight corners, with each coordinate moved inward by delta.
2. The block centre.

The retained default delta is 0.2 m. A candidate is accepted if either:

- it lies in a marked voxel of the final dilated field; or
- it lies inside an always-include volume.

Accepted candidates are appended to a GPU structured probe buffer in dispatch order;
readback order defines deterministic probe_id values.

There are no wall polygons, terrain/floor gates, split bounds, LOD rules, material
policies, distance policies, or other selection rules.

### Always-include volumes

An always-include volume is a transformed oriented box. The GPU transforms a candidate
with the supplied world-to-local matrix and accepts it if all local coordinates lie in
[-1, 1]. This direct acceptance is independent of the dilated voxel field.

## Retained DXR visibility algorithm

The TLAS has one instance per configured object. InstanceID is the dense internal
object slot. ObjectRegistry maps that slot to stable public object_id only after GPU
readback.

Each batch uses these exact dispatch dimensions:

~~~
width  = face_resolution * face_resolution
height = 6
depth  = probe_count_in_batch
~~~

The six orthogonal 90-degree faces are ordered:

~~~
-Y, +Y, +X, -X, -Z, +Z
~~~

The x dimension becomes an (u, v) pixel coordinate. HLSL uses pixel-centre offsets and
normalizes the face position, so every probe emits exactly:

~~~
6 * face_resolution * face_resolution
~~~

primary rays. Defaults retained from the reference are face_resolution 600,
max_distance 5000.0, and back-face culling.

Each ray keeps only the nearest opaque hit. Closest-hit atomically sets the bit for
(probe-local-index, object-slot) in a visibility buffer. A word represents up to 32
probes for one object. The host reads each batch, decodes bits, maps slots to stable
object IDs, sorts IDs, and builds the canonical VisibilityCatalog.

There are no recursive material hits, secondary rays, LOD filtering, distance
filtering, or custom visibility rules.

## Public configuration

The concise JSON config contains only retained core controls and always-include
volumes. All paths are relative to the configuration file.

~~~json
{
  "world": {
    "bounds": { "min": [-8, 0, -8], "max": [8, 6, 8] },
    "objects": [
      { "object_id": 100, "label": "floor", "mesh": "meshes/floor.obj" }
    ]
  },
  "voxel": { "size": 1.0, "dilation_radius": 5 },
  "probes": {
    "storage_cell_size": 4.0,
    "delta": 0.2,
    "always_include_volumes": []
  },
  "trace": { "face_resolution": 600, "max_distance": 5000.0 },
  "output": { "directory": "output", "write_json": true, "write_binary": true }
}
~~~

storage_cell_size must be exactly 4.0 in this initial version. It remains visible in
JSON to make the retained algorithm reviewable; any other value is rejected. Every
always-include volume contains one finite affine transform. The host computes and
validates its inverse before upload.

## Module boundaries

~~~text
world/     OBJ import, transforms, WorldModel, ObjectRegistry
gpu/       D3D12 device/queue/fence, resources, descriptors, shader compilation
voxel/     VolumeRasterizer and marking/dilation HLSL kernels
sampling/  ProbeGenerator and candidate/volume HLSL program
tracing/   DxrTraceProgram, acceleration structures, bitset/readback
export/    VisibilityCatalog, JSON/PFVIS writers
apps/      forge-bake orchestration only
~~~

CPU code owns validation, identity, configuration, submission order, resource
lifetimes, and output. HLSL owns retained numerical work and never receives public
object IDs.

## Errors, bounds, and output

- Fail before output when DirectX 12, DXR 1.0, or shader compilation is unavailable.
- Reject non-finite settings/transforms, invalid volume transforms, invalid bounds,
  and resource sizes that overflow D3D12 limits.
- Size the probe append buffer from represented-block count times nine; overflow is a
  hard error.
- Clear every visibility bitset before dispatch and wait for completion before
  readback.
- Preserve transactional JSON/PFVIS replacement: failed bakes never replace a prior
  successful result.

## Verification

CPU tests cover config validation, cube-face direction generation, candidate ordering,
dilation-sphere membership, volume membership, object-slot mapping, and synthetic
visibility-bitset decoding.

A local DXR smoke command bakes the owned demo chamber and checks that JSON/PFVIS
decode to the same ordered result, at least one probe exists, and all visible IDs are
configured object IDs. CI builds and runs CPU tests only; it must not claim GPU
execution on a non-GPU runner.

## Acceptance criteria

1. forge-bake imports the demo OBJ files and completes sampling plus DXR baking on a
   DXR-capable Windows system.
2. Probe output follows the retained 4 m, eight-corner-plus-centre order and
   acceptance predicate.
3. DXR dispatch is R^2 x 6 x probe_batch with retained face ordering and pixel-centre
   directions.
4. Output visibility IDs are stable object_id values, never GPU slots.
5. JSON and PFVIS represent the same ordered data and are not replaced on failure.
6. The public source tree remains neutral and contains no predecessor identifiers,
   personal identity, or predecessor assets.
