#include "parallax_forge/sampling/probe_math.hpp"

namespace parallax_forge::sampling {
namespace {

constexpr float kStorageBlockSize = 4.0f;
constexpr float kStorageBlockCentre = kStorageBlockSize * 0.5f;

float CornerCoordinate(float minimum, float delta, bool maximum) {
  return minimum + (maximum ? kStorageBlockSize - delta : delta);
}

}  // namespace

std::array<Vec3, 9> CandidatePoints(Vec3 block_min, float delta) {
  std::array<Vec3, 9> points{};
  for (std::size_t index = 0; index < 8; ++index) {
    points[index] = Vec3{
        CornerCoordinate(block_min.x, delta, (index & 1u) != 0),
        CornerCoordinate(block_min.y, delta, (index & 2u) != 0),
        CornerCoordinate(block_min.z, delta, (index & 4u) != 0)};
  }
  points[8] = Vec3{
      block_min.x + kStorageBlockCentre,
      block_min.y + kStorageBlockCentre,
      block_min.z + kStorageBlockCentre};
  return points;
}

bool AcceptCandidate(bool dilated_voxel, bool inside_volume) {
  return dilated_voxel || inside_volume;
}

}  // namespace parallax_forge::sampling
