#include "parallax_forge/sampling/probe_generator.hpp"

#include "parallax_forge/gpu/descriptor_arena.hpp"
#include "probe_generator_limits.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef PARALLAX_FORGE_PROBE_GENERATOR_DXIL
#error "Probe generator shader output path is not configured."
#endif

namespace parallax_forge::sampling {
namespace {

using Microsoft::WRL::ComPtr;

constexpr float kStorageBlockSize = 4.0f;
constexpr std::uint32_t kCandidatesPerBlock = 9;
constexpr std::uint32_t kProbeConstantCount = 16;
constexpr std::uint64_t kVolumeStride = sizeof(world::Transform);

static_assert(sizeof(Vec3) == 12);
static_assert(kVolumeStride == 64);

struct GpuVolume {
  std::array<float, 16> world_to_local;
};

static_assert(sizeof(GpuVolume) == kVolumeStride);

[[noreturn]] void ThrowHresult(const char* operation, HRESULT result) {
  std::ostringstream message;
  message << operation << " failed with HRESULT 0x" << std::hex
          << std::uppercase << static_cast<std::uint32_t>(result);
  throw std::runtime_error(message.str());
}

void ThrowIfFailed(HRESULT result, const char* operation) {
  if (FAILED(result)) {
    ThrowHresult(operation, result);
  }
}

std::vector<std::byte> ReadShader(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error("Failed to open probe shader: " +
                             path.string());
  }

  const std::streampos end = input.tellg();
  if (end <= 0) {
    throw std::runtime_error("Probe shader is empty: " + path.string());
  }
  const auto size = static_cast<std::uint64_t>(end);
  if (size > (std::numeric_limits<std::size_t>::max)()) {
    throw std::length_error("Probe shader exceeds addressable memory.");
  }

  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    throw std::runtime_error("Failed to read probe shader: " +
                             path.string());
  }
  return bytes;
}

ComPtr<ID3D12RootSignature> CreateRootSignature(
    gpu::GpuContext& context) {
  D3D12_DESCRIPTOR_RANGE probe_range{};
  probe_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  probe_range.NumDescriptors = 1;
  probe_range.BaseShaderRegister = 0;
  probe_range.RegisterSpace = 0;
  probe_range.OffsetInDescriptorsFromTableStart = 0;

  std::array<D3D12_ROOT_PARAMETER, 4> parameters{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[0].Descriptor.RegisterSpace = 0;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[1].Descriptor.ShaderRegister = 1;
  parameters[1].Descriptor.RegisterSpace = 0;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[2].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[2].DescriptorTable.NumDescriptorRanges = 1;
  parameters[2].DescriptorTable.pDescriptorRanges = &probe_range;
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[3].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[3].Constants.ShaderRegister = 0;
  parameters[3].Constants.RegisterSpace = 0;
  parameters[3].Constants.Num32BitValues = kProbeConstantCount;
  parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC description{};
  description.NumParameters = static_cast<UINT>(parameters.size());
  description.pParameters = parameters.data();
  description.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> serialized;
  ComPtr<ID3DBlob> errors;
  const HRESULT serialize_result = D3D12SerializeRootSignature(
      &description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
  if (FAILED(serialize_result)) {
    std::string message = "D3D12SerializeRootSignature failed.";
    if (errors != nullptr && errors->GetBufferSize() > 0) {
      message.append(" ");
      message.append(static_cast<const char*>(errors->GetBufferPointer()),
                     errors->GetBufferSize());
    }
    throw std::runtime_error(message);
  }

  ComPtr<ID3D12RootSignature> root_signature;
  ThrowIfFailed(
      context.device()->CreateRootSignature(
          0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
          IID_PPV_ARGS(&root_signature)),
      "ID3D12Device::CreateRootSignature");
  return root_signature;
}

ComPtr<ID3D12PipelineState> CreatePipeline(
    gpu::GpuContext& context, ID3D12RootSignature* root_signature,
    const std::vector<std::byte>& shader) {
  D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
  description.pRootSignature = root_signature;
  description.CS.pShaderBytecode = shader.data();
  description.CS.BytecodeLength = shader.size();

  ComPtr<ID3D12PipelineState> pipeline;
  ThrowIfFailed(
      context.device()->CreateComputePipelineState(
          &description, IID_PPV_ARGS(&pipeline)),
      "ID3D12Device::CreateComputePipelineState");
  return pipeline;
}

std::uint32_t BlockExtent(float minimum, float maximum) {
  const float extent = maximum - minimum;
  if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
      !std::isfinite(extent) || extent <= 0.0f) {
    throw std::invalid_argument(
        "Probe bounds must have positive finite extents.");
  }

  const double blocks =
      std::trunc(static_cast<double>(extent) / kStorageBlockSize);
  if (blocks >
      static_cast<double>((std::numeric_limits<std::uint32_t>::max)())) {
    throw std::length_error(
        "Probe block extent exceeds the shader coordinate range.");
  }
  return static_cast<std::uint32_t>(blocks);
}

std::uint64_t CheckedMultiply(std::uint64_t left, std::uint64_t right,
                              std::uint64_t limit,
                              const char* message) {
  if (right != 0 && left > limit / right) {
    throw std::length_error(message);
  }
  return left * right;
}

void ValidateSettings(const world::ProbeSettings& settings) {
  if (settings.storage_cell_size != kStorageBlockSize) {
    throw std::invalid_argument(
        "Probe storage cell size must equal 4.");
  }
  if (!std::isfinite(settings.delta) || settings.delta < 0.0f ||
      settings.delta >= kStorageBlockSize * 0.5f) {
    throw std::invalid_argument(
        "Probe delta must be finite, at least zero, and less than 2.");
  }
  const std::uint64_t volume_count =
      settings.always_include_volumes.size();
  constexpr std::uint64_t byte_address_range =
      static_cast<std::uint64_t>(
          (std::numeric_limits<std::uint32_t>::max)()) +
      1;
  if (volume_count > byte_address_range / kVolumeStride ||
      volume_count >
          (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "Always-include volumes exceed the shader byte-address range.");
  }
}

std::uint32_t CheckedVoxelCount(
    const voxel::GpuVoxelField& field) {
  if (field.grid.x == 0 || field.grid.y == 0 || field.grid.z == 0 ||
      !std::isfinite(field.grid.voxel_size) ||
      field.grid.voxel_size <= 0.0f ||
      !std::isfinite(field.grid.origin.x) ||
      !std::isfinite(field.grid.origin.y) ||
      !std::isfinite(field.grid.origin.z)) {
    throw std::invalid_argument("GPU voxel field metadata is invalid.");
  }

  constexpr std::uint64_t limit =
      (std::numeric_limits<std::uint32_t>::max)();
  std::uint64_t count = field.grid.x;
  count = CheckedMultiply(count, field.grid.y, limit,
                          "GPU voxel field exceeds shader limits.");
  count = CheckedMultiply(count, field.grid.z, limit,
                          "GPU voxel field exceeds shader limits.");
  const std::uint64_t required_bytes =
      count * sizeof(std::uint32_t);
  if (field.final_field.resource() == nullptr ||
      field.final_field.byte_size() < required_bytes) {
    throw std::invalid_argument(
        "GPU voxel field buffer is smaller than its grid.");
  }
  return static_cast<std::uint32_t>(count);
}

std::vector<GpuVolume> FlattenVolumes(
    const world::ProbeSettings& settings) {
  std::vector<GpuVolume> volumes;
  volumes.reserve(settings.always_include_volumes.size());
  for (const auto& volume : settings.always_include_volumes) {
    volumes.push_back(GpuVolume{volume.world_to_local.values});
  }
  return volumes;
}

std::array<std::uint32_t, kProbeConstantCount> ProbeConstants(
    const detail::ProbeWorkload& workload, const Bounds& bounds,
    float delta, const voxel::GridShape& grid,
    std::uint32_t volume_count) {
  return {
      workload.blocks_x,
      workload.blocks_y,
      workload.blocks_z,
      workload.block_count,
      std::bit_cast<std::uint32_t>(bounds.min.x),
      std::bit_cast<std::uint32_t>(bounds.min.y),
      std::bit_cast<std::uint32_t>(bounds.min.z),
      std::bit_cast<std::uint32_t>(delta),
      grid.x,
      grid.y,
      grid.z,
      std::bit_cast<std::uint32_t>(grid.voxel_size),
      std::bit_cast<std::uint32_t>(grid.origin.x),
      std::bit_cast<std::uint32_t>(grid.origin.y),
      std::bit_cast<std::uint32_t>(grid.origin.z),
      volume_count};
}

D3D12_RESOURCE_BARRIER TransitionBarrier(
    ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  return barrier;
}

D3D12_RESOURCE_BARRIER UavBarrier(ID3D12Resource* resource) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = resource;
  return barrier;
}

std::uint32_t ReadCount(gpu::GpuBuffer& readback) {
  void* mapped = nullptr;
  const D3D12_RANGE read_range{0, sizeof(std::uint32_t)};
  ThrowIfFailed(readback.resource()->Map(0, &read_range, &mapped),
                "ID3D12Resource::Map append counter");
  std::uint32_t count = 0;
  std::memcpy(&count, mapped, sizeof(count));
  const D3D12_RANGE written_range{0, 0};
  readback.resource()->Unmap(0, &written_range);
  return count;
}

std::vector<Vec3> ReadProbes(gpu::GpuBuffer& readback,
                             std::uint32_t count) {
  std::vector<Vec3> probes(count);
  if (count == 0) {
    return probes;
  }

  const std::size_t byte_count =
      probes.size() * sizeof(Vec3);
  void* mapped = nullptr;
  const D3D12_RANGE read_range{0, byte_count};
  ThrowIfFailed(readback.resource()->Map(0, &read_range, &mapped),
                "ID3D12Resource::Map probe output");
  std::memcpy(probes.data(), mapped, byte_count);
  const D3D12_RANGE written_range{0, 0};
  readback.resource()->Unmap(0, &written_range);
  return probes;
}

}  // namespace

detail::ProbeWorkload detail::CheckedProbeWorkload(
    const world::Bounds& bounds) {
  const std::uint32_t blocks_x =
      BlockExtent(bounds.min.x, bounds.max.x);
  const std::uint32_t blocks_y =
      BlockExtent(bounds.min.y, bounds.max.y);
  const std::uint32_t blocks_z =
      BlockExtent(bounds.min.z, bounds.max.z);
  constexpr std::uint64_t limit =
      (std::numeric_limits<std::uint32_t>::max)();
  std::uint64_t block_count =
      CheckedMultiply(blocks_x, blocks_y, limit,
                      "Probe block count exceeds shader limits.");
  block_count =
      CheckedMultiply(block_count, blocks_z, limit,
                      "Probe block count exceeds shader limits.");
  const std::uint64_t candidate_capacity =
      CheckedMultiply(block_count, kCandidatesPerBlock, limit,
                      "Probe append capacity exceeds shader limits.");
  return ProbeWorkload{
      blocks_x,
      blocks_y,
      blocks_z,
      static_cast<std::uint32_t>(block_count),
      static_cast<std::uint32_t>(candidate_capacity)};
}

void detail::ValidateAppendCount(
    std::uint32_t count, std::uint32_t candidate_capacity) {
  if (count > candidate_capacity) {
    throw std::runtime_error(
        "GPU probe append buffer capacity was exceeded.");
  }
}

struct ProbeGenerator::Impl {
  explicit Impl(gpu::GpuContext& gpu_context)
      : context(&gpu_context),
        root_signature(CreateRootSignature(gpu_context)) {
    const auto shader = ReadShader(
        std::filesystem::path(PARALLAX_FORGE_PROBE_GENERATOR_DXIL));
    pipeline =
        CreatePipeline(gpu_context, root_signature.Get(), shader);
  }

  gpu::GpuContext* context;
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline;
};

ProbeGenerator::ProbeGenerator(gpu::GpuContext& context)
    : impl_(std::make_unique<Impl>(context)) {}

ProbeGenerator::~ProbeGenerator() = default;
ProbeGenerator::ProbeGenerator(ProbeGenerator&&) noexcept = default;
ProbeGenerator& ProbeGenerator::operator=(
    ProbeGenerator&&) noexcept = default;

std::vector<Vec3> ProbeGenerator::Generate(
    const voxel::GpuVoxelField& field, const Bounds& bounds,
    const world::ProbeSettings& settings) {
  if (impl_ == nullptr || impl_->context == nullptr) {
    throw std::logic_error(
        "Cannot generate probes with a moved-from ProbeGenerator.");
  }
  ValidateSettings(settings);
  const detail::ProbeWorkload workload =
      detail::CheckedProbeWorkload(bounds);
  static_cast<void>(CheckedVoxelCount(field));
  if (workload.candidate_capacity == 0) {
    return {};
  }

  const std::uint64_t probe_bytes =
      static_cast<std::uint64_t>(workload.candidate_capacity) *
      sizeof(Vec3);
  auto probe_buffer =
      gpu::GpuBuffer::DefaultUav(*impl_->context, probe_bytes);
  auto counter_buffer =
      gpu::GpuBuffer::DefaultUav(*impl_->context,
                                 sizeof(std::uint32_t));
  auto probe_readback =
      gpu::GpuBuffer::Readback(*impl_->context, probe_bytes);
  auto counter_readback =
      gpu::GpuBuffer::Readback(*impl_->context,
                               sizeof(std::uint32_t));

  const auto volumes = FlattenVolumes(settings);
  const std::array<std::byte, sizeof(GpuVolume)> empty_volume{};
  auto volume_buffer = volumes.empty()
      ? gpu::GpuBuffer::Upload(*impl_->context, empty_volume)
      : gpu::GpuBuffer::Upload(
            *impl_->context, std::as_bytes(std::span{volumes}));

  gpu::DescriptorArena descriptors(*impl_->context, 2);
  D3D12_UNORDERED_ACCESS_VIEW_DESC probe_uav{};
  probe_uav.Format = DXGI_FORMAT_UNKNOWN;
  probe_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  probe_uav.Buffer.NumElements = workload.candidate_capacity;
  probe_uav.Buffer.StructureByteStride = sizeof(Vec3);
  probe_uav.Buffer.CounterOffsetInBytes = 0;
  impl_->context->device()->CreateUnorderedAccessView(
      probe_buffer.resource(), counter_buffer.resource(), &probe_uav,
      descriptors.cpu_handle(0));

  D3D12_UNORDERED_ACCESS_VIEW_DESC counter_uav{};
  counter_uav.Format = DXGI_FORMAT_R32_UINT;
  counter_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  counter_uav.Buffer.NumElements = 1;
  impl_->context->device()->CreateUnorderedAccessView(
      counter_buffer.resource(), nullptr, &counter_uav,
      descriptors.cpu_handle(1));

  auto* commands = impl_->context->BeginCommands();
  std::array initial_transitions{
      TransitionBarrier(
          field.final_field.resource(),
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
      TransitionBarrier(probe_buffer.resource(),
                        D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      TransitionBarrier(counter_buffer.resource(),
                        D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
  commands->ResourceBarrier(
      static_cast<UINT>(initial_transitions.size()),
      initial_transitions.data());

  ID3D12DescriptorHeap* heaps[] = {descriptors.heap()};
  commands->SetDescriptorHeaps(1, heaps);
  const std::array<UINT, 4> zero{};
  commands->ClearUnorderedAccessViewUint(
      descriptors.gpu_handle(1), descriptors.cpu_handle(1),
      counter_buffer.resource(), zero.data(), 0, nullptr);
  auto counter_clear_barrier = UavBarrier(counter_buffer.resource());
  commands->ResourceBarrier(1, &counter_clear_barrier);

  const auto constants = ProbeConstants(
      workload, bounds, settings.delta, field.grid,
      static_cast<std::uint32_t>(volumes.size()));
  commands->SetPipelineState(impl_->pipeline.Get());
  commands->SetComputeRootSignature(impl_->root_signature.Get());
  commands->SetComputeRootShaderResourceView(
      0, field.final_field.gpu_address());
  commands->SetComputeRootShaderResourceView(
      1, volume_buffer.gpu_address());
  commands->SetComputeRootDescriptorTable(
      2, descriptors.gpu_handle(0));
  commands->SetComputeRoot32BitConstants(
      3, static_cast<UINT>(constants.size()), constants.data(), 0);
  commands->Dispatch(1, 1, 1);

  std::array uav_barriers{
      UavBarrier(probe_buffer.resource()),
      UavBarrier(counter_buffer.resource())};
  commands->ResourceBarrier(
      static_cast<UINT>(uav_barriers.size()), uav_barriers.data());

  std::array final_transitions{
      TransitionBarrier(
          field.final_field.resource(),
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      TransitionBarrier(probe_buffer.resource(),
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_COPY_SOURCE),
      TransitionBarrier(counter_buffer.resource(),
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_COPY_SOURCE)};
  commands->ResourceBarrier(
      static_cast<UINT>(final_transitions.size()),
      final_transitions.data());
  commands->CopyBufferRegion(
      probe_readback.resource(), 0, probe_buffer.resource(), 0,
      probe_bytes);
  commands->CopyBufferRegion(
      counter_readback.resource(), 0, counter_buffer.resource(), 0,
      sizeof(std::uint32_t));
  impl_->context->ExecuteAndWait();

  const std::uint32_t count = ReadCount(counter_readback);
  detail::ValidateAppendCount(count, workload.candidate_capacity);
  return ReadProbes(probe_readback, count);
}

}  // namespace parallax_forge::sampling
