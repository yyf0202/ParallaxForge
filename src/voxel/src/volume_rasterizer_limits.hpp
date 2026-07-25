#pragma once

#include "parallax_forge/voxel/voxel_grid.hpp"

#include <cstdint>

namespace parallax_forge::voxel::detail {

[[nodiscard]] std::uint32_t CheckedVoxelCount(const GridShape& grid);
[[nodiscard]] std::uint64_t MaximumAddressableTriangleRecords() noexcept;
void ValidateTriangleRecordCount(std::uint64_t triangle_count);

}  // namespace parallax_forge::voxel::detail
