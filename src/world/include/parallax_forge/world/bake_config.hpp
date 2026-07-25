#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <parallax_forge/world/types.hpp>

namespace parallax_forge::world {
struct AlwaysIncludeVolume {
  Transform local_to_world;
  Transform world_to_local;
};

struct VoxelSettings {
  float size{};
  std::uint32_t dilation_radius{};
};

struct ProbeSettings {
  float storage_cell_size{};
  float delta{};
  std::vector<AlwaysIncludeVolume> always_include_volumes;
};

struct TraceSettings {
  std::uint32_t face_resolution{};
  float max_distance{};
};

struct OutputSettings {
  std::filesystem::path directory;
  bool write_json{};
  bool write_binary{};
};

struct BakeConfig {
  Bounds bounds;
  std::vector<ObjectDefinition> objects;
  VoxelSettings voxel;
  ProbeSettings probes;
  TraceSettings trace;
  OutputSettings output;
  std::filesystem::path config_directory;
};
}  // namespace parallax_forge::world
