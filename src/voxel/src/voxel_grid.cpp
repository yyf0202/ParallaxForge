#include "parallax_forge/voxel/voxel_grid.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace parallax_forge::voxel {
namespace {

std::uint32_t GridExtent(float minimum, float maximum, float voxel_size) {
  const float extent = maximum - minimum;
  if (!std::isfinite(extent) || extent <= 0.0f) {
    throw std::invalid_argument(
        "Voxel grid bounds must have positive finite extents.");
  }

  const float cells = std::ceil(extent / voxel_size);
  if (!std::isfinite(cells) || cells < 1.0f ||
      static_cast<double>(cells) >
          (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "Voxel grid extent exceeds the supported cell count.");
  }
  return static_cast<std::uint32_t>(cells);
}

std::uint64_t Magnitude(int value) {
  const auto wide = static_cast<std::int64_t>(value);
  return static_cast<std::uint64_t>(wide < 0 ? -wide : wide);
}

}  // namespace

GridShape MakeGrid(const world::Bounds& bounds, float voxel_size) {
  if (!std::isfinite(voxel_size) || voxel_size <= 0.0f) {
    throw std::invalid_argument(
        "Voxel size must be positive and finite.");
  }

  return GridShape{
      GridExtent(bounds.min.x, bounds.max.x, voxel_size),
      GridExtent(bounds.min.y, bounds.max.y, voxel_size),
      GridExtent(bounds.min.z, bounds.max.z, voxel_size),
      bounds.min,
      voxel_size};
}

bool InDilationSphere(int dx, int dy, int dz, std::uint32_t radius) {
  const std::uint64_t x = Magnitude(dx);
  const std::uint64_t y = Magnitude(dy);
  const std::uint64_t z = Magnitude(dz);
  if (x > radius || y > radius || z > radius) {
    return false;
  }

  std::uint64_t remaining =
      static_cast<std::uint64_t>(radius) * radius;
  const std::uint64_t x_squared = x * x;
  if (x_squared > remaining) {
    return false;
  }
  remaining -= x_squared;

  const std::uint64_t y_squared = y * y;
  if (y_squared > remaining) {
    return false;
  }
  remaining -= y_squared;
  return z * z <= remaining;
}

}  // namespace parallax_forge::voxel
