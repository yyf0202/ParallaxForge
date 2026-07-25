#include <parallax_forge/tracing/cube_faces.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace parallax_forge::tracing {

std::array<CubeFaceDirection, 6> CubeFaceDirections() {
  return {
      CubeFaceDirection{
          {-1.0f, -1.0f, -1.0f},
          {2.0f, 0.0f, 0.0f},
          {0.0f, 0.0f, 2.0f},
          {0.0f, -1.0f, 0.0f}},
      CubeFaceDirection{
          {-1.0f, 1.0f, 1.0f},
          {2.0f, 0.0f, 0.0f},
          {0.0f, 0.0f, -2.0f},
          {0.0f, 1.0f, 0.0f}},
      CubeFaceDirection{
          {1.0f, -1.0f, 1.0f},
          {0.0f, 2.0f, 0.0f},
          {0.0f, 0.0f, -2.0f},
          {1.0f, 0.0f, 0.0f}},
      CubeFaceDirection{
          {-1.0f, -1.0f, -1.0f},
          {0.0f, 2.0f, 0.0f},
          {0.0f, 0.0f, 2.0f},
          {-1.0f, 0.0f, 0.0f}},
      CubeFaceDirection{
          {1.0f, -1.0f, -1.0f},
          {-2.0f, 0.0f, 0.0f},
          {0.0f, 2.0f, 0.0f},
          {0.0f, 0.0f, -1.0f}},
      CubeFaceDirection{
          {-1.0f, -1.0f, 1.0f},
          {2.0f, 0.0f, 0.0f},
          {0.0f, 2.0f, 0.0f},
          {0.0f, 0.0f, 1.0f}},
  };
}

world::Vec3 CubeFacePixelDirection(
    std::uint32_t face_index, std::uint32_t linear_pixel,
    std::uint32_t face_resolution) {
  if (face_resolution == 0) {
    throw std::invalid_argument(
        "Cube-face resolution must be greater than zero.");
  }

  const auto faces = CubeFaceDirections();
  if (face_index >= faces.size()) {
    throw std::out_of_range("Cube-face index is outside the six faces.");
  }

  const std::uint64_t pixel_count =
      static_cast<std::uint64_t>(face_resolution) * face_resolution;
  if (linear_pixel >= pixel_count) {
    throw std::out_of_range("Cube-face pixel is outside the face.");
  }

  const std::uint32_t pixel_x = linear_pixel % face_resolution;
  const std::uint32_t pixel_y = linear_pixel / face_resolution;
  const float u =
      (static_cast<float>(pixel_x) + 0.5f) /
      static_cast<float>(face_resolution);
  const float v =
      (static_cast<float>(pixel_y) + 0.5f) /
      static_cast<float>(face_resolution);
  const auto& face = faces[face_index];
  world::Vec3 direction{
      face.origin.x + u * face.extend_u.x + v * face.extend_v.x,
      face.origin.y + u * face.extend_u.y + v * face.extend_v.y,
      face.origin.z + u * face.extend_u.z + v * face.extend_v.z};
  const float length = std::sqrt(
      direction.x * direction.x + direction.y * direction.y +
      direction.z * direction.z);
  direction.x /= length;
  direction.y /= length;
  direction.z /= length;
  return direction;
}

}  // namespace parallax_forge::tracing
