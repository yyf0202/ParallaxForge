#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <parallax_forge/world/types.hpp>

namespace parallax_forge::world {
struct VoxelSettings {
  float size{};
  float clearance{};
};

struct ProbeSettings {
  float spacing{};
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
};
}  // namespace parallax_forge::world
