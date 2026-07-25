#pragma once

#include "parallax_forge/gpu/gpu_context.hpp"
#include "parallax_forge/voxel/volume_rasterizer.hpp"
#include "parallax_forge/world/bake_config.hpp"
#include "parallax_forge/world/types.hpp"

#include <memory>
#include <vector>

namespace parallax_forge::sampling {

using world::Bounds;
using world::Vec3;

class ProbeGenerator {
 public:
  explicit ProbeGenerator(gpu::GpuContext& context);
  ~ProbeGenerator();

  ProbeGenerator(ProbeGenerator&&) noexcept;
  ProbeGenerator& operator=(ProbeGenerator&&) noexcept;
  ProbeGenerator(const ProbeGenerator&) = delete;
  ProbeGenerator& operator=(const ProbeGenerator&) = delete;

  [[nodiscard]] std::vector<Vec3> Generate(
      const voxel::GpuVoxelField& field, const Bounds& bounds,
      const world::ProbeSettings& settings);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace parallax_forge::sampling
