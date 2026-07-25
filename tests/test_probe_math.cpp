#include "parallax_forge/gpu/gpu_buffer.hpp"
#include "parallax_forge/gpu/gpu_context.hpp"
#include "parallax_forge/sampling/probe_generator.hpp"
#include "parallax_forge/sampling/probe_math.hpp"
#include "parallax_forge/voxel/volume_rasterizer.hpp"
#include "probe_generator_limits.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using parallax_forge::gpu::GpuBuffer;
using parallax_forge::gpu::GpuContext;
using parallax_forge::sampling::AcceptCandidate;
using parallax_forge::sampling::CandidatePoints;
using parallax_forge::sampling::ProbeGenerator;
namespace probe_detail = parallax_forge::sampling::detail;
using parallax_forge::voxel::GpuVoxelField;
using parallax_forge::voxel::GridShape;
using parallax_forge::world::AlwaysIncludeVolume;
using parallax_forge::world::Bounds;
using parallax_forge::world::ProbeSettings;
using parallax_forge::world::Transform;
using parallax_forge::world::Vec3;

constexpr float kTolerance = 0.0001f;

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void AssertNear(float actual, float expected) {
  if (std::fabs(actual - expected) <= kTolerance) {
    return;
  }
  std::ostringstream message;
  message << "Probe coordinate mismatch: expected " << expected
          << ", actual " << actual << '.';
  throw std::runtime_error(message.str());
}

void AssertPoint(Vec3 actual, Vec3 expected) {
  AssertNear(actual.x, expected.x);
  AssertNear(actual.y, expected.y);
  AssertNear(actual.z, expected.z);
}

void VerifyCandidateOrderAndAcceptance() {
  const auto points = CandidatePoints(Vec3{0.0f, 0.0f, 0.0f}, 0.2f);
  const std::array expected{
      Vec3{0.2f, 0.2f, 0.2f},
      Vec3{3.8f, 0.2f, 0.2f},
      Vec3{0.2f, 3.8f, 0.2f},
      Vec3{3.8f, 3.8f, 0.2f},
      Vec3{0.2f, 0.2f, 3.8f},
      Vec3{3.8f, 0.2f, 3.8f},
      Vec3{0.2f, 3.8f, 3.8f},
      Vec3{3.8f, 3.8f, 3.8f},
      Vec3{2.0f, 2.0f, 2.0f}};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    AssertPoint(points[index], expected[index]);
  }

  Require(AcceptCandidate(true, false),
          "Dilated voxel candidate was rejected.");
  Require(AcceptCandidate(false, true),
          "Always-include candidate was rejected.");
  Require(!AcceptCandidate(false, false),
          "Unaccepted candidate was retained.");
}

void VerifyHostWorkloadChecks() {
  const auto workload = probe_detail::CheckedProbeWorkload(
      Bounds{{0.0f, 0.0f, 0.0f}, {8.5f, 4.5f, 4.5f}});
  Require(workload.blocks_x == 2, "Trailing X block was not truncated.");
  Require(workload.blocks_y == 1, "Trailing Y block was not truncated.");
  Require(workload.blocks_z == 1, "Trailing Z block was not truncated.");
  Require(workload.block_count == 2, "Probe block count is incorrect.");
  Require(workload.candidate_capacity == 18,
          "Probe candidate capacity is incorrect.");

  bool block_count_overflow_rejected = false;
  try {
    static_cast<void>(probe_detail::CheckedProbeWorkload(
        Bounds{{0.0f, 0.0f, 0.0f}, {262144.0f, 262144.0f, 4.0f}}));
  } catch (const std::length_error&) {
    block_count_overflow_rejected = true;
  }
  Require(block_count_overflow_rejected,
          "Probe block-count overflow was accepted.");

  bool candidate_count_overflow_rejected = false;
  try {
    static_cast<void>(probe_detail::CheckedProbeWorkload(
        Bounds{{0.0f, 0.0f, 0.0f}, {262144.0f, 32768.0f, 4.0f}}));
  } catch (const std::length_error&) {
    candidate_count_overflow_rejected = true;
  }
  Require(candidate_count_overflow_rejected,
          "Probe candidate-count overflow was accepted.");

  const auto short_axis_workload = probe_detail::CheckedProbeWorkload(
      Bounds{{0.0f, 0.0f, 0.0f}, {8.0f, 3.9f, 4.0f}});
  Require(short_axis_workload.blocks_y == 0,
          "Sub-4m axis unexpectedly represented a storage block.");
  Require(short_axis_workload.candidate_capacity == 0,
          "Sub-4m axis unexpectedly generated probe candidates.");

  probe_detail::ValidateAppendCount(9, 9);
  bool excess_count_rejected = false;
  try {
    probe_detail::ValidateAppendCount(10, 9);
  } catch (const std::runtime_error&) {
    excess_count_rejected = true;
  }
  Require(excess_count_rejected,
          "Excess GPU append count was accepted.");
}

bool IsUnavailableHardware(const std::runtime_error& error) {
  return std::string_view(error.what()).starts_with(
      "No hardware Direct3D 12 adapter with DXR tier 1.0 and "
      "Int64ShaderOps support");
}

GpuVoxelField MakeField(GpuContext& context, GridShape grid,
                        std::span<const std::uint32_t> values) {
  const auto bytes = std::as_bytes(values);
  auto upload = GpuBuffer::Upload(context, bytes);
  auto field = GpuBuffer::DefaultUav(context, bytes.size());

  auto* commands = context.BeginCommands();
  D3D12_RESOURCE_BARRIER to_copy{};
  to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  to_copy.Transition.pResource = field.resource();
  to_copy.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  commands->ResourceBarrier(1, &to_copy);
  commands->CopyBufferRegion(field.resource(), 0, upload.resource(), 0,
                             bytes.size());

  auto to_uav = to_copy;
  to_uav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  to_uav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  commands->ResourceBarrier(1, &to_uav);
  context.ExecuteAndWait();

  return GpuVoxelField{grid, std::move(field)};
}

void VerifyVoxelAcceptanceOrderAndTrailingBlocks(
    ProbeGenerator& generator, GpuContext& context) {
  const GridShape grid{8, 4, 4, Vec3{0.0f, 0.0f, 0.0f}, 1.0f};
  const std::vector<std::uint32_t> occupied(
      static_cast<std::size_t>(grid.x) * grid.y * grid.z, 1);
  auto field = MakeField(context, grid, occupied);
  const Bounds bounds{{0.0f, 0.0f, 0.0f}, {8.5f, 4.5f, 4.5f}};
  const ProbeSettings settings{4.0f, 0.2f, {}};

  const auto probes = generator.Generate(field, bounds, settings);
  Require(probes.size() == 18,
          "Full-capacity probe append did not return all 18 candidates.");

  const auto first_block =
      CandidatePoints(Vec3{0.0f, 0.0f, 0.0f}, settings.delta);
  const auto second_block =
      CandidatePoints(Vec3{4.0f, 0.0f, 0.0f}, settings.delta);
  for (std::size_t index = 0; index < first_block.size(); ++index) {
    AssertPoint(probes[index], first_block[index]);
    AssertPoint(probes[index + first_block.size()], second_block[index]);
  }

  for (const auto& probe : probes) {
    Require(probe.x < 8.0f, "Probe entered the trailing X block.");
    Require(probe.y < 4.0f, "Probe entered the trailing Y block.");
    Require(probe.z < 4.0f, "Probe entered the trailing Z block.");
  }
}

void VerifySparseVoxelAcceptance(ProbeGenerator& generator,
                                 GpuContext& context) {
  const GridShape grid{4, 4, 4, Vec3{0.0f, 0.0f, 0.0f}, 1.0f};
  std::vector<std::uint32_t> values(
      static_cast<std::size_t>(grid.x) * grid.y * grid.z, 0);
  values.front() = 1;
  auto field = MakeField(context, grid, values);

  const auto probes = generator.Generate(
      field, Bounds{{0.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 4.0f}},
      ProbeSettings{4.0f, 0.2f, {}});
  Require(probes.size() == 1,
          "Sparse final voxel field did not accept exactly one candidate.");
  AssertPoint(probes.front(), Vec3{0.2f, 0.2f, 0.2f});
}

void VerifyRotatedScaledVolumeOutsideVoxelField(
    ProbeGenerator& generator, GpuContext& context) {
  const GridShape grid{1, 1, 1, Vec3{0.0f, 0.0f, 0.0f}, 1.0f};
  const std::array<std::uint32_t, 1> empty{};
  auto field = MakeField(context, grid, empty);

  constexpr float cosine = 0.7071067812f;
  constexpr float sine = 0.7071067812f;
  constexpr float scale_x = 2.5f;
  constexpr float scale_y = 0.5f;
  constexpr float scale_z = 0.4f;
  constexpr float centre_x = 9.7f;
  constexpr float centre_y = 1.7f;
  constexpr float centre_z = 0.2f;
  const Transform local_to_world{std::array<float, 16>{
      scale_x * cosine, scale_x * sine, 0.0f, 0.0f,
      -scale_y * sine, scale_y * cosine, 0.0f, 0.0f,
      0.0f, 0.0f, scale_z, 0.0f,
      centre_x, centre_y, centre_z, 1.0f}};
  const Transform world_to_local{std::array<float, 16>{
      cosine / scale_x, -sine / scale_y, 0.0f, 0.0f,
      sine / scale_x, cosine / scale_y, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f / scale_z, 0.0f,
      -(centre_x * cosine + centre_y * sine) / scale_x,
      (centre_x * sine - centre_y * cosine) / scale_y,
      -centre_z / scale_z, 1.0f}};
  const ProbeSettings settings{
      4.0f,
      0.2f,
      std::vector<AlwaysIncludeVolume>{
          AlwaysIncludeVolume{local_to_world, world_to_local}}};
  const Bounds bounds{{8.0f, 0.0f, 0.0f}, {12.0f, 4.0f, 4.0f}};

  const auto probes = generator.Generate(field, bounds, settings);
  Require(probes.size() == 1,
          "Rotated scaled volume did not accept exactly one candidate.");
  AssertPoint(probes.front(), Vec3{8.2f, 0.2f, 0.2f});
}

int VerifyGpuProbeGenerator() {
  try {
    auto context = GpuContext::Create();
    ProbeGenerator generator(context);
    VerifyVoxelAcceptanceOrderAndTrailingBlocks(generator, context);
    VerifySparseVoxelAcceptance(generator, context);
    VerifyRotatedScaledVolumeOutsideVoxelField(generator, context);
  } catch (const std::runtime_error& error) {
    if (IsUnavailableHardware(error)) {
      return 77;
    }
    throw;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    VerifyCandidateOrderAndAcceptance();
    VerifyHostWorkloadChecks();
    if (argc == 2 && std::string_view(argv[1]) == "--gpu") {
      return VerifyGpuProbeGenerator();
    }
    Require(argc == 1, "Unexpected probe test arguments.");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
