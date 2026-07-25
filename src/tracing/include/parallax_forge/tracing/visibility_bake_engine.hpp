#pragma once

#include <memory>
#include <span>

#include <parallax_forge/export/visibility_catalog.hpp>
#include <parallax_forge/gpu/gpu_context.hpp>
#include <parallax_forge/world/bake_config.hpp>
#include <parallax_forge/world/world_model.hpp>

namespace parallax_forge::tracing {

class VisibilityBakeEngine {
 public:
  explicit VisibilityBakeEngine(gpu::GpuContext& context);
  ~VisibilityBakeEngine();

  VisibilityBakeEngine(VisibilityBakeEngine&&) noexcept;
  VisibilityBakeEngine& operator=(VisibilityBakeEngine&&) noexcept;
  VisibilityBakeEngine(const VisibilityBakeEngine&) = delete;
  VisibilityBakeEngine& operator=(const VisibilityBakeEngine&) = delete;

  [[nodiscard]] export_data::VisibilityCatalog Bake(
      const world::WorldModel& world, std::span<const world::Vec3> probes,
      const world::TraceSettings& settings);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace parallax_forge::tracing
