#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <parallax_forge/gpu/gpu_context.hpp>
#include <parallax_forge/world/bake_config.hpp>
#include <parallax_forge/world/world_model.hpp>

namespace parallax_forge::tracing {

namespace detail {

struct TraceDispatchDimensions {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t depth{};
  std::uint64_t ray_count{};
};

[[nodiscard]] std::uint32_t MaximumTraceProbeBatch(
    std::uint32_t face_resolution);

[[nodiscard]] TraceDispatchDimensions CheckedTraceDispatchDimensions(
    std::uint32_t face_resolution, std::uint32_t batch_probe_count);

}  // namespace detail

struct VisibilityTraceBatch {
  std::uint32_t first_probe{};
  std::uint32_t probe_count{};
  std::vector<std::uint32_t> words;
};

class DxrTraceProgram {
 public:
  explicit DxrTraceProgram(gpu::GpuContext& context);
  ~DxrTraceProgram();

  DxrTraceProgram(DxrTraceProgram&&) noexcept;
  DxrTraceProgram& operator=(DxrTraceProgram&&) noexcept;
  DxrTraceProgram(const DxrTraceProgram&) = delete;
  DxrTraceProgram& operator=(const DxrTraceProgram&) = delete;

  [[nodiscard]] std::vector<VisibilityTraceBatch> Trace(
      const world::WorldModel& world, std::span<const world::Vec3> probes,
      const world::TraceSettings& settings);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace parallax_forge::tracing
