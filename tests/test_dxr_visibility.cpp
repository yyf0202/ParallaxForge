#include <parallax_forge/gpu/gpu_context.hpp>
#include <parallax_forge/tracing/visibility_bake_engine.hpp>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

namespace tracing = parallax_forge::tracing;
namespace world = parallax_forge::world;

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

world::Transform Translation(float x, float y, float z) {
  auto transform = world::Transform::Identity();
  transform.values[12] = x;
  transform.values[13] = y;
  transform.values[14] = z;
  return transform;
}

std::vector<world::Triangle> NegativeXSquare() {
  return {
      world::Triangle{
          {0.0f, -1.0f, -1.0f},
          {0.0f, -1.0f, 1.0f},
          {0.0f, 1.0f, 1.0f}},
      world::Triangle{
          {0.0f, -1.0f, -1.0f},
          {0.0f, 1.0f, 1.0f},
          {0.0f, 1.0f, -1.0f}}};
}

std::vector<world::Triangle> NegativeZSquare() {
  return {
      world::Triangle{
          {-1.0f, -1.0f, 0.0f},
          {-1.0f, 1.0f, 0.0f},
          {1.0f, 1.0f, 0.0f}},
      world::Triangle{
          {-1.0f, -1.0f, 0.0f},
          {1.0f, 1.0f, 0.0f},
          {1.0f, -1.0f, 0.0f}}};
}

bool IsUnavailableHardware(const std::runtime_error& error) {
  return std::string_view(error.what()).starts_with(
      "No hardware Direct3D 12 adapter with DXR tier 1.0 support");
}

int VerifyHardwareDxrVisibility() {
  try {
    auto context = parallax_forge::gpu::GpuContext::Create();
    tracing::VisibilityBakeEngine engine(context);

    const auto x_square = NegativeXSquare();
    const world::WorldModel scene{
        world::Bounds{{-1.0f, -1.0f, -1.0f}, {5.0f, 1.0f, 5.0f}},
        {
            world::ImportedObject{
                42, 0, Translation(0.0f, 0.0f, 2.0f),
                NegativeZSquare()},
            world::ImportedObject{
                700, 1, Translation(2.0f, 0.0f, 0.0f), x_square},
            world::ImportedObject{
                900, 2, Translation(4.0f, 0.0f, 0.0f), x_square},
        }};
    const std::vector<world::Vec3> probes(33, world::Vec3{});

    const auto catalog =
        engine.Bake(scene, probes, world::TraceSettings{1, 10.0f});
    Require(catalog.Probes().size() == probes.size(),
            "DXR bake did not return every probe.");
    for (std::uint32_t index = 0; index < probes.size(); ++index) {
      const auto& result = catalog.Probes()[index];
      Require(result.probe_id == index, "DXR probe ID changed.");
      if (result.visible_object_ids !=
          std::vector<world::ObjectId>({42, 700})) {
        std::cerr << "probe " << index << " visible IDs:";
        for (const auto object_id : result.visible_object_ids) {
          std::cerr << ' ' << object_id;
        }
        std::cerr << '\n';
        throw std::runtime_error(
            "DXR nearest-hit visibility or stable-ID decoding changed.");
      }
    }
  } catch (const std::runtime_error& error) {
    if (IsUnavailableHardware(error)) {
      return 77;
    }
    throw;
  }
  return 0;
}

}  // namespace

int main() {
  try {
    return VerifyHardwareDxrVisibility();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
