#pragma once

#include "parallax_forge/gpu/gpu_buffer.hpp"
#include "parallax_forge/voxel/voxel_grid.hpp"
#include "parallax_forge/world/bake_config.hpp"
#include "parallax_forge/world/world_model.hpp"

#include <memory>

namespace parallax_forge::voxel {

struct GpuVoxelField {
  GridShape grid;
  gpu::GpuBuffer final_field;
};

class VolumeRasterizer {
 public:
  explicit VolumeRasterizer(gpu::GpuContext& context);
  ~VolumeRasterizer();

  VolumeRasterizer(VolumeRasterizer&&) noexcept;
  VolumeRasterizer& operator=(VolumeRasterizer&&) noexcept;
  VolumeRasterizer(const VolumeRasterizer&) = delete;
  VolumeRasterizer& operator=(const VolumeRasterizer&) = delete;

  [[nodiscard]] GpuVoxelField Rasterize(
      const world::WorldModel& world,
      const world::VoxelSettings& settings);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace parallax_forge::voxel
