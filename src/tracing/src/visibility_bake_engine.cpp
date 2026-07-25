#include <parallax_forge/tracing/visibility_bake_engine.hpp>

#include <parallax_forge/tracing/dxr_trace_program.hpp>
#include <parallax_forge/tracing/visibility_bitset.hpp>
#include <parallax_forge/world/object_registry.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace parallax_forge::tracing {
namespace {

world::ObjectRegistry RegistryForWorld(
    const world::WorldModel& world_model) {
  constexpr std::size_t maximum_tlas_instances = 0x00ffffffu;
  if (world_model.objects.size() > maximum_tlas_instances) {
    throw std::length_error(
        "World object count exceeds the DXR TLAS instance limit.");
  }
  std::vector<world::ObjectDefinition> definitions;
  definitions.reserve(world_model.objects.size());
  for (const auto& object : world_model.objects) {
    definitions.push_back(world::ObjectDefinition{
        object.object_id, {}, {}, object.local_to_world});
  }
  world::ObjectRegistry registry(std::move(definitions));
  for (const auto& object : world_model.objects) {
    const auto slot = registry.SlotFor(object.object_id);
    if (!slot.has_value() || *slot != object.slot) {
      throw std::invalid_argument(
          "World object slots must be dense stable-registry slots.");
    }
  }
  return registry;
}

void ValidateProbes(std::span<const world::Vec3> probes) {
  if (probes.size() >
      (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "Probe count exceeds the public probe-ID range.");
  }
  for (const auto& probe : probes) {
    if (!std::isfinite(probe.x) || !std::isfinite(probe.y) ||
        !std::isfinite(probe.z)) {
      throw std::invalid_argument(
          "Trace probe positions must be finite.");
    }
  }
}

}  // namespace

struct VisibilityBakeEngine::Impl {
  explicit Impl(gpu::GpuContext& context) : trace_program(context) {}

  DxrTraceProgram trace_program;
};

VisibilityBakeEngine::VisibilityBakeEngine(gpu::GpuContext& context)
    : impl_(std::make_unique<Impl>(context)) {}

VisibilityBakeEngine::~VisibilityBakeEngine() = default;
VisibilityBakeEngine::VisibilityBakeEngine(
    VisibilityBakeEngine&&) noexcept = default;
VisibilityBakeEngine& VisibilityBakeEngine::operator=(
    VisibilityBakeEngine&&) noexcept = default;

export_data::VisibilityCatalog VisibilityBakeEngine::Bake(
    const world::WorldModel& world_model,
    std::span<const world::Vec3> probes,
    const world::TraceSettings& settings) {
  if (impl_ == nullptr) {
    throw std::logic_error(
        "Cannot bake visibility with a moved-from engine.");
  }
  if (!std::isfinite(settings.max_distance) ||
      settings.max_distance <= 0.0f) {
    throw std::invalid_argument(
        "Trace maximum distance must be positive and finite.");
  }
  static_cast<void>(
      detail::MaximumTraceProbeBatch(settings.face_resolution));
  ValidateProbes(probes);
  const world::ObjectRegistry registry =
      RegistryForWorld(world_model);
  export_data::VisibilityCatalog catalog(registry.Objects());

  if (probes.empty() || world_model.objects.empty()) {
    for (std::uint32_t probe_id = 0; probe_id < probes.size();
         ++probe_id) {
      catalog.AddProbe(export_data::ProbeVisibility{
          probe_id, probes[probe_id], {}});
    }
    return catalog;
  }

  const auto batches =
      impl_->trace_program.Trace(world_model, probes, settings);
  std::uint32_t expected_first_probe = 0;
  for (const auto& batch : batches) {
    if (batch.first_probe != expected_first_probe ||
        batch.probe_count == 0 ||
        batch.first_probe >
            static_cast<std::uint32_t>(probes.size()) ||
        batch.probe_count >
            static_cast<std::uint32_t>(probes.size()) -
                batch.first_probe) {
      throw std::runtime_error(
          "DXR trace returned an invalid probe batch.");
    }
    const std::uint64_t expected_words =
        CheckedVisibilityWordCount(
            registry.Objects().size(), batch.probe_count);
    if (expected_words != batch.words.size()) {
      throw std::runtime_error(
          "DXR trace returned an invalid visibility word count.");
    }

    for (std::uint32_t local_probe = 0;
         local_probe < batch.probe_count; ++local_probe) {
      const std::uint32_t probe_id =
          batch.first_probe + local_probe;
      catalog.AddProbe(export_data::ProbeVisibility{
          probe_id, probes[probe_id],
          DecodeVisibilityWord(
              batch.words, local_probe, registry)});
    }
    expected_first_probe += batch.probe_count;
  }
  if (expected_first_probe != probes.size()) {
    throw std::runtime_error(
        "DXR trace did not return every probe.");
  }
  return catalog;
}

}  // namespace parallax_forge::tracing
