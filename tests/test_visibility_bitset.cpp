#include <parallax_forge/tracing/cube_faces.hpp>
#include <parallax_forge/tracing/dxr_trace_program.hpp>
#include <parallax_forge/tracing/visibility_bitset.hpp>
#include <parallax_forge/world/object_registry.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

namespace tracing = parallax_forge::tracing;
namespace world = parallax_forge::world;

constexpr float kTolerance = 0.00001f;

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void RequireNear(float actual, float expected, const char* message) {
  if (std::fabs(actual - expected) > kTolerance) {
    throw std::runtime_error(message);
  }
}

void RequireVec(world::Vec3 actual, world::Vec3 expected,
                const char* message) {
  RequireNear(actual.x, expected.x, message);
  RequireNear(actual.y, expected.y, message);
  RequireNear(actual.z, expected.z, message);
}

template <typename Exception, typename Function>
void RequireThrows(Function&& function, const char* message) {
  try {
    function();
  } catch (const Exception&) {
    return;
  }
  throw std::runtime_error(message);
}

world::ObjectDefinition Object(world::ObjectId object_id) {
  return world::ObjectDefinition{
      object_id, {}, {}, world::Transform::Identity()};
}

void VerifyCubeFacesAndPixelCentres() {
  const auto faces = tracing::CubeFaceDirections();
  const std::array expected_normals{
      world::Vec3{0.0f, -1.0f, 0.0f},
      world::Vec3{0.0f, 1.0f, 0.0f},
      world::Vec3{1.0f, 0.0f, 0.0f},
      world::Vec3{-1.0f, 0.0f, 0.0f},
      world::Vec3{0.0f, 0.0f, -1.0f},
      world::Vec3{0.0f, 0.0f, 1.0f}};
  const std::array expected_origins{
      world::Vec3{-1.0f, -1.0f, -1.0f},
      world::Vec3{-1.0f, 1.0f, 1.0f},
      world::Vec3{1.0f, -1.0f, 1.0f},
      world::Vec3{-1.0f, -1.0f, -1.0f},
      world::Vec3{1.0f, -1.0f, -1.0f},
      world::Vec3{-1.0f, -1.0f, 1.0f}};
  const std::array expected_u{
      world::Vec3{2.0f, 0.0f, 0.0f},
      world::Vec3{2.0f, 0.0f, 0.0f},
      world::Vec3{0.0f, 2.0f, 0.0f},
      world::Vec3{0.0f, 2.0f, 0.0f},
      world::Vec3{-2.0f, 0.0f, 0.0f},
      world::Vec3{2.0f, 0.0f, 0.0f}};
  const std::array expected_v{
      world::Vec3{0.0f, 0.0f, 2.0f},
      world::Vec3{0.0f, 0.0f, -2.0f},
      world::Vec3{0.0f, 0.0f, -2.0f},
      world::Vec3{0.0f, 0.0f, 2.0f},
      world::Vec3{0.0f, 2.0f, 0.0f},
      world::Vec3{0.0f, 2.0f, 0.0f}};

  for (std::uint32_t face = 0; face < faces.size(); ++face) {
    RequireVec(faces[face].normal, expected_normals[face],
               "Cube-face normal order changed.");
    RequireVec(faces[face].origin, expected_origins[face],
               "Cube-face origin changed.");
    RequireVec(faces[face].extend_u, expected_u[face],
               "Cube-face U extent changed.");
    RequireVec(faces[face].extend_v, expected_v[face],
               "Cube-face V extent changed.");
    RequireVec(tracing::CubeFacePixelDirection(face, 0, 1),
               expected_normals[face],
               "One-pixel cube face did not trace its normal.");
  }

  constexpr float inverse_length = 0.4082482904638631f;
  RequireVec(tracing::CubeFacePixelDirection(0, 0, 2),
             world::Vec3{-inverse_length, -2.0f * inverse_length,
                         -inverse_length},
             "Cube-face pixel-centre normalization changed.");
  RequireThrows<std::out_of_range>(
      [] { static_cast<void>(tracing::CubeFacePixelDirection(6, 0, 1)); },
      "Out-of-range cube face was accepted.");
  RequireThrows<std::out_of_range>(
      [] { static_cast<void>(tracing::CubeFacePixelDirection(0, 4, 2)); },
      "Out-of-range cube-face pixel was accepted.");
  RequireThrows<std::invalid_argument>(
      [] { static_cast<void>(tracing::CubeFacePixelDirection(0, 0, 0)); },
      "Zero cube-face resolution was accepted.");
}

void VerifyVisibilityLayoutAndStableDecode() {
  const world::ObjectRegistry registry(
      {Object(30), Object(10), Object(20)});
  const std::vector<std::uint32_t> one_word_each{
      0b10u, 0b00u, 0b10u};
  const auto ids =
      tracing::DecodeVisibilityWord(one_word_each, 1, registry);
  Require(ids == std::vector<world::ObjectId>({10, 30}),
          "Visibility slots were not decoded to sorted stable IDs.");

  Require(tracing::VisibilityWordsPerObject(0) == 0,
          "Zero probes unexpectedly consume visibility words.");
  Require(tracing::VisibilityWordsPerObject(31) == 1,
          "31 probes should consume one word per object.");
  Require(tracing::VisibilityWordsPerObject(32) == 1,
          "32 probes should consume one word per object.");
  Require(tracing::VisibilityWordsPerObject(33) == 2,
          "33 probes should consume two words per object.");
  Require(tracing::VisibilityWordsPerObject(63) == 2,
          "63 probes should consume two words per object.");
  Require(tracing::VisibilityWordsPerObject(64) == 2,
          "64 probes should consume two words per object.");
  Require(tracing::VisibilityWordsPerObject(65) == 3,
          "65 probes should consume three words per object.");

  Require(tracing::CheckedVisibilityWordCount(3, 32) == 3,
          "32-probe visibility word count changed.");
  Require(tracing::CheckedVisibilityWordCount(3, 33) == 6,
          "33-probe visibility word count changed.");
  Require(tracing::CheckedVisibilityByteCount(3, 33) == 24,
          "Visibility byte count changed.");
  Require(tracing::MaximumVisibilityProbeBatch(0) ==
              (std::numeric_limits<std::uint32_t>::max)(),
          "An empty object set unexpectedly limits probe batches.");
  Require(tracing::MaximumVisibilityProbeBatch(1) ==
              (std::numeric_limits<std::uint32_t>::max)(),
          "One object unexpectedly limits probe batches.");
  Require(tracing::MaximumVisibilityProbeBatch(32) == 4294967264u,
          "Visibility word addressing did not constrain 32 objects.");
  Require(tracing::MaximumVisibilityProbeBatch(
              D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE) ==
              8160u,
          "Maximum TLAS object count used an unsafe probe batch.");
  Require(tracing::CheckedVisibilityWordIndex(0, 3, 31, 33) == 0,
          "Probe 31 used the wrong first-object word.");
  Require(tracing::CheckedVisibilityWordIndex(0, 3, 32, 33) == 1,
          "Probe 32 did not cross the first-object word boundary.");
  Require(tracing::CheckedVisibilityWordIndex(2, 3, 31, 33) == 4,
          "Last object used the wrong first word.");
  Require(tracing::CheckedVisibilityWordIndex(2, 3, 32, 33) == 5,
          "Last object used the wrong second word.");
  Require(tracing::VisibilityProbeMask(31) == 0x80000000u,
          "Probe 31 used the wrong bit.");
  Require(tracing::VisibilityProbeMask(32) == 1u,
          "Probe 32 did not wrap to bit zero.");

  const std::vector<std::uint32_t> two_words_each{
      0u, 1u,
      0u, 0u,
      0u, 1u};
  Require(tracing::DecodeVisibilityWord(two_words_each, 32, registry) ==
              std::vector<world::ObjectId>({10, 30}),
          "Second visibility word decoded incorrect stable IDs.");

  RequireThrows<std::out_of_range>(
      [] {
        static_cast<void>(
            tracing::CheckedVisibilityWordIndex(3, 3, 0, 33));
      },
      "Out-of-range object slot was accepted.");
  RequireThrows<std::out_of_range>(
      [] {
        static_cast<void>(
            tracing::CheckedVisibilityWordIndex(0, 3, 33, 33));
      },
      "Out-of-range local probe was accepted.");
  RequireThrows<std::invalid_argument>(
      [&] {
        const std::array<std::uint32_t, 5> malformed{};
        static_cast<void>(
            tracing::DecodeVisibilityWord(malformed, 0, registry));
      },
      "Non-divisible visibility word layout was accepted.");
  RequireThrows<std::out_of_range>(
      [&] {
        static_cast<void>(
            tracing::DecodeVisibilityWord(two_words_each, 64, registry));
      },
      "Probe outside the visibility word span was accepted.");
  RequireThrows<std::length_error>(
      [] {
        static_cast<void>(tracing::CheckedVisibilityWordCount(
            (std::numeric_limits<std::uint64_t>::max)(), 33));
      },
      "Visibility word-count overflow was accepted.");
  RequireThrows<std::length_error>(
      [] {
        constexpr std::uint64_t maximum_words =
            (std::numeric_limits<std::uint64_t>::max)() /
            sizeof(std::uint32_t);
        static_cast<void>(
            tracing::CheckedVisibilityByteCount(maximum_words + 1u, 1));
      },
      "Visibility byte-count overflow was accepted.");
  RequireThrows<std::length_error>(
      [] {
        static_cast<void>(tracing::MaximumVisibilityProbeBatch(
            static_cast<std::uint64_t>(
                (std::numeric_limits<std::uint32_t>::max)()) +
            1u));
      },
      "Unaddressable object count produced a visibility batch.");
}

void VerifyDispatchArithmetic() {
  tracing::detail::ValidateTraceInstanceCount(
      D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE);
  tracing::detail::ValidateTraceInstanceSlot(
      static_cast<world::InstanceSlot>(
          D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE -
          1u));
  tracing::detail::ValidateBlasPrimitiveCount(
      D3D12_RAYTRACING_MAX_PRIMITIVES_PER_BOTTOM_LEVEL_ACCELERATION_STRUCTURE);
  RequireThrows<std::length_error>(
      [] {
        tracing::detail::ValidateTraceInstanceCount(
            static_cast<std::uint64_t>(
                D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE) +
            1u);
      },
      "TLAS object count above the SDK limit was accepted.");
  RequireThrows<std::out_of_range>(
      [] {
        tracing::detail::ValidateTraceInstanceSlot(
            static_cast<world::InstanceSlot>(
                D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE));
      },
      "TLAS slot above the 24-bit InstanceID range was accepted.");
  RequireThrows<std::length_error>(
      [] {
        tracing::detail::ValidateBlasPrimitiveCount(
            static_cast<std::uint64_t>(
                D3D12_RAYTRACING_MAX_PRIMITIVES_PER_BOTTOM_LEVEL_ACCELERATION_STRUCTURE) +
            1u);
      },
      "BLAS primitive count above the SDK limit was accepted.");

  const auto dispatch =
      tracing::detail::CheckedTraceDispatchDimensions(3, 33);
  Require(dispatch.width == 9, "DXR dispatch width is not R squared.");
  Require(dispatch.height == 6, "DXR dispatch height is not six faces.");
  Require(dispatch.depth == 33, "DXR dispatch depth is not the batch size.");
  Require(dispatch.ray_count == 9u * 6u * 33u,
          "DXR dispatch ray count changed.");
  Require(tracing::detail::MaximumTraceProbeBatch(600) == 497,
          "Default-resolution probe batch limit changed.");
  RequireThrows<std::invalid_argument>(
      [] {
        static_cast<void>(
            tracing::detail::CheckedTraceDispatchDimensions(0, 1));
      },
      "Zero trace resolution was accepted.");
  RequireThrows<std::invalid_argument>(
      [] {
        static_cast<void>(
            tracing::detail::CheckedTraceDispatchDimensions(1, 0));
      },
      "Zero trace batch was accepted.");
  RequireThrows<std::length_error>(
      [] {
        static_cast<void>(
            tracing::detail::CheckedTraceDispatchDimensions(65536, 1));
      },
      "Overflowing R-squared dispatch width was accepted.");
}

}  // namespace

int main() {
  try {
    VerifyCubeFacesAndPixelCentres();
    VerifyVisibilityLayoutAndStableDecode();
    VerifyDispatchArithmetic();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
