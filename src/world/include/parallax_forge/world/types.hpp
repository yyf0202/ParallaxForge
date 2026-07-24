#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace parallax_forge::world {
using ObjectId = std::uint32_t;
using InstanceSlot = std::uint32_t;

struct Vec3 {
  float x{};
  float y{};
  float z{};
};

struct Bounds {
  Vec3 min;
  Vec3 max;
};

struct Transform {
  std::array<float, 16> values;

  static Transform Identity() {
    return Transform{std::array<float, 16>{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f}};
  }
};

struct ObjectDefinition {
  ObjectId object_id{};
  std::string label;
  std::string mesh_path;
  Transform transform{Transform::Identity()};
};
}  // namespace parallax_forge::world
