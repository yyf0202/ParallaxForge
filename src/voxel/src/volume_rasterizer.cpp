#include "parallax_forge/voxel/volume_rasterizer.hpp"

#include "parallax_forge/gpu/descriptor_arena.hpp"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef PARALLAX_FORGE_VOLUME_RASTERIZER_MARK_DXIL
#error "MarkTriangles shader output path is not configured."
#endif

#ifndef PARALLAX_FORGE_VOLUME_RASTERIZER_DILATE_DXIL
#error "DilateVoxels shader output path is not configured."
#endif

namespace parallax_forge::voxel {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kThreadGroupSize = 64;
constexpr std::uint32_t kMaximumDispatchDimension = 65535;
constexpr std::uint32_t kRasterConstantCount = 10;

struct GpuTriangle {
  std::array<float, 3> a;
  std::array<float, 3> b;
  std::array<float, 3> c;
  std::array<float, 16> local_to_world;
};

static_assert(sizeof(GpuTriangle) == 100);

struct DispatchShape {
  std::uint32_t x;
  std::uint32_t y;
};

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
    throw std::runtime_error("Failed to open voxel shader: " +
                             path.string());
  }

  const std::streampos end = input.tellg();
  if (end <= 0) {
    throw std::runtime_error("Voxel shader is empty: " + path.string());
  }
  const auto size = static_cast<std::uint64_t>(end);
  if (size > (std::numeric_limits<std::size_t>::max)()) {
    throw std::length_error("Voxel shader exceeds addressable memory.");
  }

  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    throw std::runtime_error("Failed to read voxel shader: " +
                             path.string());
  }
  return bytes;
}

ComPtr<ID3D12RootSignature> CreateRootSignature(gpu::GpuContext& context) {
  std::array<D3D12_ROOT_PARAMETER, 4> parameters{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[0].Descriptor.RegisterSpace = 0;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[1].Descriptor.ShaderRegister = 0;
  parameters[1].Descriptor.RegisterSpace = 0;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  parameters[2].Descriptor.ShaderRegister = 1;
  parameters[2].Descriptor.RegisterSpace = 0;
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[3].Constants.ShaderRegister = 0;
  parameters[3].Constants.RegisterSpace = 0;
  parameters[3].Constants.Num32BitValues = kRasterConstantCount;
  parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC description{};
  description.NumParameters = static_cast<UINT>(parameters.size());
  description.pParameters = parameters.data();
  description.NumStaticSamplers = 0;
  description.pStaticSamplers = nullptr;
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

DispatchShape MakeDispatch(std::uint32_t item_count) {
  if (item_count == 0) {
    throw std::invalid_argument(
        "A GPU dispatch must contain at least one item.");
  }

  const std::uint64_t group_count =
      (static_cast<std::uint64_t>(item_count) + kThreadGroupSize - 1) /
      kThreadGroupSize;
  const std::uint32_t x = static_cast<std::uint32_t>(
      (std::min)(group_count,
                 static_cast<std::uint64_t>(kMaximumDispatchDimension)));
  const std::uint64_t y =
      (group_count + static_cast<std::uint64_t>(x) - 1) / x;
  if (y > kMaximumDispatchDimension) {
    throw std::length_error(
        "Voxel workload exceeds Direct3D 12 dispatch dimensions.");
  }
  return DispatchShape{x, static_cast<std::uint32_t>(y)};
}

std::array<float, 3> Position(const world::Vec3& value) {
  return {value.x, value.y, value.z};
}

std::vector<GpuTriangle> FlattenTriangles(
    const world::WorldModel& world) {
  std::size_t triangle_count = 0;
  for (const auto& object : world.objects) {
    if (object.triangles.size() >
        (std::numeric_limits<std::size_t>::max)() - triangle_count) {
      throw std::length_error("World triangle count exceeds addressable memory.");
    }
    triangle_count += object.triangles.size();
  }
  if (triangle_count >
      (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "World triangle count exceeds the GPU rasterizer limit.");
  }

  std::vector<GpuTriangle> flattened;
  flattened.reserve(triangle_count);
  for (const auto& object : world.objects) {
    for (const auto& triangle : object.triangles) {
      flattened.push_back(GpuTriangle{
          Position(triangle.a),
          Position(triangle.b),
          Position(triangle.c),
          object.local_to_world.values});
    }
  }
  return flattened;
}

std::uint32_t VoxelCount(const GridShape& grid) {
  const std::uint64_t xy =
      static_cast<std::uint64_t>(grid.x) * grid.y;
  const std::uint64_t xyz = xy * grid.z;
  if (xyz == 0 ||
      xyz > (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "Voxel grid exceeds the GPU rasterizer limit.");
  }
  return static_cast<std::uint32_t>(xyz);
}

std::array<std::uint32_t, kRasterConstantCount> RasterConstants(
    const GridShape& grid, std::uint32_t item_count,
    std::uint32_t dilation_radius, std::uint32_t dispatch_groups_x) {
  return {
      grid.x,
      grid.y,
      grid.z,
      item_count,
      std::bit_cast<std::uint32_t>(grid.origin.x),
      std::bit_cast<std::uint32_t>(grid.origin.y),
      std::bit_cast<std::uint32_t>(grid.origin.z),
      std::bit_cast<std::uint32_t>(grid.voxel_size),
      dilation_radius,
      dispatch_groups_x};
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

}  // namespace

struct VolumeRasterizer::Impl {
  explicit Impl(gpu::GpuContext& gpu_context)
      : context(&gpu_context),
        root_signature(CreateRootSignature(gpu_context)) {
    const auto mark_shader = ReadShader(
        std::filesystem::path(PARALLAX_FORGE_VOLUME_RASTERIZER_MARK_DXIL));
    const auto dilate_shader = ReadShader(
        std::filesystem::path(PARALLAX_FORGE_VOLUME_RASTERIZER_DILATE_DXIL));
    mark_pipeline =
        CreatePipeline(gpu_context, root_signature.Get(), mark_shader);
    dilate_pipeline =
        CreatePipeline(gpu_context, root_signature.Get(), dilate_shader);
  }

  gpu::GpuContext* context;
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> mark_pipeline;
  ComPtr<ID3D12PipelineState> dilate_pipeline;
};

VolumeRasterizer::VolumeRasterizer(gpu::GpuContext& context)
    : impl_(std::make_unique<Impl>(context)) {}

VolumeRasterizer::~VolumeRasterizer() = default;
VolumeRasterizer::VolumeRasterizer(VolumeRasterizer&&) noexcept = default;
VolumeRasterizer& VolumeRasterizer::operator=(
    VolumeRasterizer&&) noexcept = default;

GpuVoxelField VolumeRasterizer::Rasterize(
    const world::WorldModel& world,
    const world::VoxelSettings& settings) {
  if (impl_ == nullptr || impl_->context == nullptr) {
    throw std::logic_error(
        "Cannot rasterize with a moved-from VolumeRasterizer.");
  }
  if (settings.dilation_radius >
      static_cast<std::uint32_t>((std::numeric_limits<int>::max)())) {
    throw std::length_error(
        "Voxel dilation radius exceeds shader coordinate range.");
  }

  const GridShape grid = MakeGrid(world.bounds, settings.size);
  if (grid.x >
          static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
      grid.y >
          static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
      grid.z >
          static_cast<std::uint32_t>((std::numeric_limits<int>::max)())) {
    throw std::length_error(
        "Voxel grid dimensions exceed shader coordinate range.");
  }
  const std::uint32_t voxel_count = VoxelCount(grid);
  const std::uint64_t field_bytes =
      static_cast<std::uint64_t>(voxel_count) * sizeof(std::uint32_t);

  auto temporary_field =
      gpu::GpuBuffer::DefaultUav(*impl_->context, field_bytes);
  auto final_field =
      gpu::GpuBuffer::DefaultUav(*impl_->context, field_bytes);
  const auto triangles = FlattenTriangles(world);

  std::optional<gpu::GpuBuffer> triangle_buffer;
  if (!triangles.empty()) {
    triangle_buffer.emplace(gpu::GpuBuffer::Upload(
        *impl_->context, std::as_bytes(std::span{triangles})));
  }

  gpu::DescriptorArena clear_descriptor(*impl_->context, 1);
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_description{};
  uav_description.Format = DXGI_FORMAT_UNKNOWN;
  uav_description.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_description.Buffer.FirstElement = 0;
  uav_description.Buffer.NumElements = voxel_count;
  uav_description.Buffer.StructureByteStride = sizeof(std::uint32_t);
  uav_description.Buffer.CounterOffsetInBytes = 0;
  uav_description.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
  impl_->context->device()->CreateUnorderedAccessView(
      temporary_field.resource(), nullptr, &uav_description,
      clear_descriptor.cpu_handle(0));

  auto* commands = impl_->context->BeginCommands();
  std::array transitions{
      TransitionBarrier(temporary_field.resource(),
                        D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
      TransitionBarrier(final_field.resource(),
                        D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
  commands->ResourceBarrier(static_cast<UINT>(transitions.size()),
                            transitions.data());

  ID3D12DescriptorHeap* heaps[] = {clear_descriptor.heap()};
  commands->SetDescriptorHeaps(1, heaps);
  const std::array<UINT, 4> clear_values{};
  commands->ClearUnorderedAccessViewUint(
      clear_descriptor.gpu_handle(0), clear_descriptor.cpu_handle(0),
      temporary_field.resource(), clear_values.data(), 0, nullptr);

  auto temporary_barrier = UavBarrier(temporary_field.resource());
  commands->ResourceBarrier(1, &temporary_barrier);

  if (triangle_buffer.has_value()) {
    const auto triangle_count =
        static_cast<std::uint32_t>(triangles.size());
    const DispatchShape dispatch = MakeDispatch(triangle_count);
    const auto constants =
        RasterConstants(grid, triangle_count, settings.dilation_radius,
                        dispatch.x);
    commands->SetPipelineState(impl_->mark_pipeline.Get());
    commands->SetComputeRootSignature(impl_->root_signature.Get());
    commands->SetComputeRootShaderResourceView(
        0, triangle_buffer->gpu_address());
    commands->SetComputeRootUnorderedAccessView(
        1, temporary_field.gpu_address());
    commands->SetComputeRoot32BitConstants(
        3, static_cast<UINT>(constants.size()), constants.data(), 0);
    commands->Dispatch(dispatch.x, dispatch.y, 1);
    commands->ResourceBarrier(1, &temporary_barrier);
  }

  const DispatchShape dilation_dispatch = MakeDispatch(voxel_count);
  const auto dilation_constants =
      RasterConstants(grid, voxel_count, settings.dilation_radius,
                      dilation_dispatch.x);
  commands->SetPipelineState(impl_->dilate_pipeline.Get());
  commands->SetComputeRootSignature(impl_->root_signature.Get());
  commands->SetComputeRootUnorderedAccessView(
      1, temporary_field.gpu_address());
  commands->SetComputeRootUnorderedAccessView(
      2, final_field.gpu_address());
  commands->SetComputeRoot32BitConstants(
      3, static_cast<UINT>(dilation_constants.size()),
      dilation_constants.data(), 0);
  commands->Dispatch(dilation_dispatch.x, dilation_dispatch.y, 1);

  auto final_barrier = UavBarrier(final_field.resource());
  commands->ResourceBarrier(1, &final_barrier);
  impl_->context->ExecuteAndWait();

  return GpuVoxelField{grid, std::move(final_field)};
}

}  // namespace parallax_forge::voxel
