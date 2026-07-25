#pragma once

#include "parallax_forge/world/types.hpp"

#include <cstdint>

namespace parallax_forge::sampling::detail {

struct ProbeWorkload {
  std::uint32_t blocks_x;
  std::uint32_t blocks_y;
  std::uint32_t blocks_z;
  std::uint32_t block_count;
  std::uint32_t candidate_capacity;
};

[[nodiscard]] ProbeWorkload CheckedProbeWorkload(
    const world::Bounds& bounds);
void ValidateAppendCount(std::uint32_t count,
                         std::uint32_t candidate_capacity);

}  // namespace parallax_forge::sampling::detail
