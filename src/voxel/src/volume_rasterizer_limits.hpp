#pragma once

#include "parallax_forge/voxel/voxel_grid.hpp"

#include <cstdint>

#include <parallax_forge/world/world_model.hpp>

namespace parallax_forge::voxel::detail {

[[nodiscard]] std::uint32_t CheckedVoxelCount(const GridShape& grid);
[[nodiscard]] std::uint64_t MaximumAddressableTriangleRecords() noexcept;
void ValidateTriangleRecordCount(std::uint64_t triangle_count);
void ValidateFiniteTransformedTriangle(
    const world::Triangle& triangle,
    const world::Transform& local_to_world);

}  // namespace parallax_forge::voxel::detail
