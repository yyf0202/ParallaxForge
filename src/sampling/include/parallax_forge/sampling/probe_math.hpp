#pragma once

#include "parallax_forge/world/types.hpp"

#include <array>

namespace parallax_forge::sampling {

using world::Vec3;

[[nodiscard]] std::array<Vec3, 9> CandidatePoints(Vec3 block_min,
                                                  float delta);
[[nodiscard]] bool AcceptCandidate(bool dilated_voxel,
                                   bool inside_volume);

}  // namespace parallax_forge::sampling
