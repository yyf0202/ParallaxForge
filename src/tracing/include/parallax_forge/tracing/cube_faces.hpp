#pragma once

#include <array>
#include <cstdint>

#include <parallax_forge/world/types.hpp>

namespace parallax_forge::tracing {

struct CubeFaceDirection {
  world::Vec3 origin;
  world::Vec3 extend_u;
  world::Vec3 extend_v;
  world::Vec3 normal;
};

[[nodiscard]] std::array<CubeFaceDirection, 6> CubeFaceDirections();

[[nodiscard]] world::Vec3 CubeFacePixelDirection(
    std::uint32_t face_index, std::uint32_t linear_pixel,
    std::uint32_t face_resolution);

}  // namespace parallax_forge::tracing
