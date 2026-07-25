#pragma once

#include "parallax_forge/world/types.hpp"

#include <cstdint>

namespace parallax_forge::voxel {

struct GridShape {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t z{};
  world::Vec3 origin;
  float voxel_size{};
};

[[nodiscard]] GridShape MakeGrid(const world::Bounds& bounds,
                                 float voxel_size);
[[nodiscard]] bool InDilationSphere(int dx, int dy, int dz,
                                    std::uint32_t radius);

}  // namespace parallax_forge::voxel
