#include <parallax_forge/tracing/dxr_trace_program.hpp>

#include <parallax_forge/gpu/descriptor_arena.hpp>
#include <parallax_forge/gpu/gpu_buffer.hpp>
#include <parallax_forge/tracing/cube_faces.hpp>
#include <parallax_forge/tracing/visibility_bitset.hpp>

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifndef PARALLAX_FORGE_PVS_TRACE_DXIL
#error "PVS trace shader output path is not configured."
#endif

namespace parallax_forge::tracing {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint64_t kMaximumRayDispatchDimension = 1ull << 30u;
constexpr std::uint32_t kCubeFaceCount = 6;
constexpr std::uint32_t kShaderRecordSize =
    D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
constexpr std::uint32_t kShaderIdentifierSize =
    D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
constexpr std::uint32_t kShaderTableRecordCount = 3;

static_assert(kShaderRecordSize >= kShaderIdentifierSize);
static_assert(kShaderRecordSize %
                  D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT ==
              0);
static_assert(sizeof(world::Vec3) == 12);
static_assert(sizeof(world::Triangle) == 36);

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

std::uint64_t RaysPerProbe(std::uint32_t face_resolution) {
  if (face_resolution == 0) {
    throw std::invalid_argument(
        "Trace face resolution must be greater than zero.");
  }
  const std::uint64_t width =
      static_cast<std::uint64_t>(face_resolution) * face_resolution;
  if (width >
      (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "Trace face resolution overflows the dispatch width.");
  }
  const std::uint64_t rays_per_probe = width * kCubeFaceCount;
  if (rays_per_probe > kMaximumRayDispatchDimension) {
    throw std::length_error(
        "One probe exceeds the DXR ray-dispatch limit.");
  }
  return rays_per_probe;
}

std::vector<std::byte> ReadShader(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error(
        "Failed to open PVS trace shader: " + path.string());
  }
  const std::streampos end = input.tellg();
  if (end <= 0) {
    throw std::runtime_error(
        "PVS trace shader is empty: " + path.string());
  }
  const auto size = static_cast<std::uint64_t>(end);
  if (size > (std::numeric_limits<std::size_t>::max)()) {
    throw std::length_error(
        "PVS trace shader exceeds addressable memory.");
  }

  std::vector<std::byte> shader(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(shader.data()),
             static_cast<std::streamsize>(shader.size()));
  if (!input) {
    throw std::runtime_error(
        "Failed to read PVS trace shader: " + path.string());
  }
  return shader;
}

ComPtr<ID3D12Resource> CreateDefaultBuffer(
    gpu::GpuContext& context, std::uint64_t byte_count,
    D3D12_RESOURCE_STATES initial_state) {
  if (byte_count == 0) {
    throw std::invalid_argument(
        "DXR resource size must be greater than zero.");
  }

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1;
  heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC description{};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = byte_count;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.Format = DXGI_FORMAT_UNKNOWN;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  ComPtr<ID3D12Resource> resource;
  ThrowIfFailed(
      context.device()->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &description, initial_state,
          nullptr, IID_PPV_ARGS(&resource)),
      "ID3D12Device::CreateCommittedResource for DXR");
  return resource;
}

ComPtr<ID3D12RootSignature> CreateRootSignature(
    gpu::GpuContext& context) {
  D3D12_DESCRIPTOR_RANGE visibility_range{};
  visibility_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  visibility_range.NumDescriptors = 1;
  visibility_range.BaseShaderRegister = 0;
  visibility_range.RegisterSpace = 0;
  visibility_range.OffsetInDescriptorsFromTableStart = 0;

  std::array<D3D12_ROOT_PARAMETER, 4> parameters{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[0].Descriptor.ShaderRegister = 0;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  parameters[1].Descriptor.ShaderRegister = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[2].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[2].DescriptorTable.NumDescriptorRanges = 1;
  parameters[2].DescriptorTable.pDescriptorRanges = &visibility_range;
  parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[3].Descriptor.ShaderRegister = 0;
  parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC description{};
  description.NumParameters = static_cast<UINT>(parameters.size());
  description.pParameters = parameters.data();
  description.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> serialized;
  ComPtr<ID3DBlob> errors;
  const HRESULT result = D3D12SerializeRootSignature(
      &description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
  if (FAILED(result)) {
    std::string message = "D3D12SerializeRootSignature failed.";
    if (errors != nullptr && errors->GetBufferSize() != 0) {
      message.append(" ");
      message.append(
          static_cast<const char*>(errors->GetBufferPointer()),
          errors->GetBufferSize());
    }
    throw std::runtime_error(message);
  }

  ComPtr<ID3D12RootSignature> root_signature;
  ThrowIfFailed(
      context.device()->CreateRootSignature(
          0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
          IID_PPV_ARGS(&root_signature)),
      "ID3D12Device::CreateRootSignature for DXR");
  return root_signature;
}

ComPtr<ID3D12StateObject> CreateStateObject(
    gpu::GpuContext& context, ID3D12RootSignature* root_signature,
    std::span<const std::byte> shader) {
  const std::array<D3D12_EXPORT_DESC, 3> exports{
      D3D12_EXPORT_DESC{
          L"RayGeneration", nullptr, D3D12_EXPORT_FLAG_NONE},
      D3D12_EXPORT_DESC{L"Miss", nullptr, D3D12_EXPORT_FLAG_NONE},
      D3D12_EXPORT_DESC{
          L"ClosestHit", nullptr, D3D12_EXPORT_FLAG_NONE}};

  D3D12_DXIL_LIBRARY_DESC library{};
  library.DXILLibrary.pShaderBytecode = shader.data();
  library.DXILLibrary.BytecodeLength = shader.size();
  library.NumExports = static_cast<UINT>(exports.size());
  library.pExports = exports.data();

  D3D12_HIT_GROUP_DESC hit_group{};
  hit_group.HitGroupExport = L"HitGroup";
  hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hit_group.ClosestHitShaderImport = L"ClosestHit";

  D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
  shader_config.MaxPayloadSizeInBytes = sizeof(std::uint32_t);
  shader_config.MaxAttributeSizeInBytes =
      D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;

  D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature{};
  global_root_signature.pGlobalRootSignature = root_signature;

  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{};
  pipeline_config.MaxTraceRecursionDepth = 1;

  std::array<D3D12_STATE_SUBOBJECT, 5> subobjects{};
  subobjects[0] = D3D12_STATE_SUBOBJECT{
      D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library};
  subobjects[1] = D3D12_STATE_SUBOBJECT{
      D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group};
  subobjects[2] = D3D12_STATE_SUBOBJECT{
      D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG,
      &shader_config};
  subobjects[3] = D3D12_STATE_SUBOBJECT{
      D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,
      &global_root_signature};
  subobjects[4] = D3D12_STATE_SUBOBJECT{
      D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,
      &pipeline_config};

  D3D12_STATE_OBJECT_DESC description{};
  description.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  description.NumSubobjects = static_cast<UINT>(subobjects.size());
  description.pSubobjects = subobjects.data();

  ComPtr<ID3D12StateObject> state_object;
  ThrowIfFailed(
      context.device()->CreateStateObject(
          &description, IID_PPV_ARGS(&state_object)),
      "ID3D12Device5::CreateStateObject");
  return state_object;
}

gpu::GpuBuffer CreateShaderTable(
    gpu::GpuContext& context, ID3D12StateObject* state_object) {
  ComPtr<ID3D12StateObjectProperties> properties;
  ThrowIfFailed(state_object->QueryInterface(IID_PPV_ARGS(&properties)),
                "ID3D12StateObject::QueryInterface properties");

  const std::array<const wchar_t*, kShaderTableRecordCount> names{
      L"RayGeneration", L"Miss", L"HitGroup"};
  std::array<std::byte,
             kShaderRecordSize * kShaderTableRecordCount>
      bytes{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const void* identifier =
        properties->GetShaderIdentifier(names[index]);
    if (identifier == nullptr) {
      throw std::runtime_error(
          "DXR state object did not expose a shader identifier.");
    }
    std::memcpy(
        bytes.data() + index * kShaderRecordSize, identifier,
        kShaderIdentifierSize);
  }
  return gpu::GpuBuffer::Upload(context, bytes);
}

bool SameMesh(std::span<const world::Triangle> left,
              std::span<const world::Triangle> right) {
  return left.size() == right.size() &&
      (left.empty() ||
       std::memcmp(left.data(), right.data(),
                   left.size_bytes()) == 0);
}

D3D12_RESOURCE_BARRIER UavBarrier(ID3D12Resource* resource) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = resource;
  return barrier;
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

struct BlasResources {
  gpu::GpuBuffer vertices;
  ComPtr<ID3D12Resource> scratch;
  ComPtr<ID3D12Resource> result;
};

struct SceneResources {
  std::vector<BlasResources> blases;
  std::optional<gpu::GpuBuffer> instance_descriptions;
  ComPtr<ID3D12Resource> tlas_scratch;
  ComPtr<ID3D12Resource> tlas_result;
};

void SetInstanceTransform(
    D3D12_RAYTRACING_INSTANCE_DESC& instance,
    const world::Transform& transform) {
  for (std::uint32_t row = 0; row < 3; ++row) {
    for (std::uint32_t column = 0; column < 4; ++column) {
      const float value = transform.values[column * 4u + row];
      if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "Object transform contains a non-finite value.");
      }
      instance.Transform[row][column] = value;
    }
  }
  for (std::uint32_t index = 0; index < 16; ++index) {
    if (!std::isfinite(transform.values[index])) {
      throw std::invalid_argument(
          "Object transform contains a non-finite value.");
    }
  }
}

SceneResources BuildScene(gpu::GpuContext& context,
                          const world::WorldModel& world_model) {
  if (world_model.objects.empty()) {
    throw std::invalid_argument(
        "Cannot build an empty DXR acceleration structure.");
  }
  detail::ValidateTraceInstanceCount(world_model.objects.size());

  SceneResources scene;
  scene.blases.reserve(world_model.objects.size());
  std::vector<const std::vector<world::Triangle>*> unique_meshes;
  unique_meshes.reserve(world_model.objects.size());
  std::vector<std::size_t> object_blas_indices;
  object_blas_indices.reserve(world_model.objects.size());

  auto* commands = context.BeginCommands();
  for (const auto& object : world_model.objects) {
    if (object.triangles.empty()) {
      throw std::invalid_argument(
          "Every traced object must contain at least one triangle.");
    }

    const auto found = std::find_if(
        unique_meshes.begin(), unique_meshes.end(),
        [&](const auto* mesh) {
          return SameMesh(*mesh, object.triangles);
        });
    if (found != unique_meshes.end()) {
      object_blas_indices.push_back(
          static_cast<std::size_t>(
              std::distance(unique_meshes.begin(), found)));
      continue;
    }

    detail::ValidateBlasPrimitiveCount(object.triangles.size());
    const std::uint64_t vertex_count =
        static_cast<std::uint64_t>(object.triangles.size()) * 3u;
    auto vertices = gpu::GpuBuffer::Upload(
        context,
        std::as_bytes(
            std::span<const world::Triangle>(object.triangles)));

    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
    geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.Transform3x4 = 0;
    geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
    geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.Triangles.IndexCount = 0;
    geometry.Triangles.VertexCount =
        static_cast<UINT>(vertex_count);
    geometry.Triangles.IndexBuffer = 0;
    geometry.Triangles.VertexBuffer.StartAddress =
        vertices.gpu_address();
    geometry.Triangles.VertexBuffer.StrideInBytes =
        sizeof(world::Vec3);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = 1;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = &geometry;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    context.device()->GetRaytracingAccelerationStructurePrebuildInfo(
        &inputs, &info);
    if (info.ResultDataMaxSizeInBytes == 0 ||
        info.ScratchDataSizeInBytes == 0) {
      throw std::runtime_error(
          "DXR returned an empty BLAS prebuild size.");
    }

    auto scratch = CreateDefaultBuffer(
        context, info.ScratchDataSizeInBytes,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto result = CreateDefaultBuffer(
        context, info.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.ScratchAccelerationStructureData =
        scratch->GetGPUVirtualAddress();
    build.DestAccelerationStructureData =
        result->GetGPUVirtualAddress();
    commands->BuildRaytracingAccelerationStructure(
        &build, 0, nullptr);
    auto barrier = UavBarrier(result.Get());
    commands->ResourceBarrier(1, &barrier);

    scene.blases.push_back(BlasResources{
        std::move(vertices), std::move(scratch), std::move(result)});
    unique_meshes.push_back(&object.triangles);
    object_blas_indices.push_back(scene.blases.size() - 1u);
  }

  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances(
      world_model.objects.size());
  for (std::size_t index = 0; index < world_model.objects.size();
       ++index) {
    const auto& object = world_model.objects[index];
    detail::ValidateTraceInstanceSlot(object.slot);
    auto& instance = instances[index];
    SetInstanceTransform(instance, object.local_to_world);
    instance.InstanceID = object.slot;
    instance.InstanceMask = 0xff;
    instance.InstanceContributionToHitGroupIndex = 0;
    instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    instance.AccelerationStructure =
        scene.blases[object_blas_indices[index]]
            .result->GetGPUVirtualAddress();
  }
  scene.instance_descriptions.emplace(gpu::GpuBuffer::Upload(
      context, std::as_bytes(std::span{instances})));

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
  tlas_inputs.Type =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.Flags =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  tlas_inputs.NumDescs = static_cast<UINT>(instances.size());
  tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_inputs.InstanceDescs =
      scene.instance_descriptions->gpu_address();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info{};
  context.device()->GetRaytracingAccelerationStructurePrebuildInfo(
      &tlas_inputs, &tlas_info);
  if (tlas_info.ResultDataMaxSizeInBytes == 0 ||
      tlas_info.ScratchDataSizeInBytes == 0) {
    throw std::runtime_error(
        "DXR returned an empty TLAS prebuild size.");
  }
  scene.tlas_scratch = CreateDefaultBuffer(
      context, tlas_info.ScratchDataSizeInBytes,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  scene.tlas_result = CreateDefaultBuffer(
      context, tlas_info.ResultDataMaxSizeInBytes,
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build{};
  tlas_build.Inputs = tlas_inputs;
  tlas_build.ScratchAccelerationStructureData =
      scene.tlas_scratch->GetGPUVirtualAddress();
  tlas_build.DestAccelerationStructureData =
      scene.tlas_result->GetGPUVirtualAddress();
  commands->BuildRaytracingAccelerationStructure(
      &tlas_build, 0, nullptr);
  auto tlas_barrier = UavBarrier(scene.tlas_result.Get());
  commands->ResourceBarrier(1, &tlas_barrier);
  context.ExecuteAndWait();
  return scene;
}

struct Float4 {
  float x;
  float y;
  float z;
  float w;
};

struct TraceConstants {
  std::array<Float4, kCubeFaceCount> face_origins;
  std::array<Float4, kCubeFaceCount> face_extents_u;
  std::array<Float4, kCubeFaceCount> face_extents_v;
  std::uint32_t face_resolution;
  std::uint32_t words_per_object;
  float max_distance;
  std::uint32_t padding;
};

static_assert(sizeof(Float4) == 16);
static_assert(sizeof(TraceConstants) == 304);

Float4 ToFloat4(world::Vec3 value) {
  return Float4{value.x, value.y, value.z, 0.0f};
}

TraceConstants MakeTraceConstants(
    const world::TraceSettings& settings,
    std::uint32_t words_per_object) {
  TraceConstants constants{};
  const auto faces = CubeFaceDirections();
  for (std::size_t index = 0; index < faces.size(); ++index) {
    constants.face_origins[index] = ToFloat4(faces[index].origin);
    constants.face_extents_u[index] =
        ToFloat4(faces[index].extend_u);
    constants.face_extents_v[index] =
        ToFloat4(faces[index].extend_v);
  }
  constants.face_resolution = settings.face_resolution;
  constants.words_per_object = words_per_object;
  constants.max_distance = settings.max_distance;
  return constants;
}

std::vector<std::uint32_t> ReadVisibilityWords(
    gpu::GpuBuffer& readback, std::uint64_t word_count) {
  if (word_count >
      (std::numeric_limits<std::size_t>::max)()) {
    throw std::length_error(
        "Visibility readback exceeds addressable memory.");
  }
  std::vector<std::uint32_t> words(
      static_cast<std::size_t>(word_count));
  const std::size_t byte_count =
      words.size() * sizeof(std::uint32_t);
  void* mapped = nullptr;
  const D3D12_RANGE read_range{0, byte_count};
  ThrowIfFailed(readback.resource()->Map(0, &read_range, &mapped),
                "ID3D12Resource::Map visibility readback");
  std::memcpy(words.data(), mapped, byte_count);
  const D3D12_RANGE written_range{0, 0};
  readback.resource()->Unmap(0, &written_range);
  return words;
}

VisibilityTraceBatch DispatchBatch(
    gpu::GpuContext& context, ID3D12RootSignature* root_signature,
    ID3D12StateObject* state_object,
    const gpu::GpuBuffer& shader_table,
    const SceneResources& scene, std::uint32_t first_probe,
    std::span<const world::Vec3> probes,
    const world::TraceSettings& settings,
    std::uint32_t object_count) {
  if (probes.size() >
      (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "Trace batch exceeds the shader probe-index range.");
  }
  const auto batch_count =
      static_cast<std::uint32_t>(probes.size());
  const auto dispatch = detail::CheckedTraceDispatchDimensions(
      settings.face_resolution, batch_count);
  const std::uint32_t words_per_object =
      VisibilityWordsPerObject(batch_count);
  const std::uint64_t word_count =
      CheckedVisibilityWordCount(object_count, batch_count);
  if (word_count >
      (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "Visibility buffer exceeds the shader word-address range.");
  }
  const std::uint64_t byte_count =
      CheckedVisibilityByteCount(object_count, batch_count);
  if (byte_count >
      (std::numeric_limits<std::size_t>::max)()) {
    throw std::length_error(
        "Visibility buffer exceeds addressable memory.");
  }

  auto probe_buffer =
      gpu::GpuBuffer::Upload(context, std::as_bytes(probes));
  auto visibility_buffer =
      gpu::GpuBuffer::DefaultUav(context, byte_count);
  auto visibility_readback =
      gpu::GpuBuffer::Readback(context, byte_count);
  const TraceConstants trace_constants =
      MakeTraceConstants(settings, words_per_object);
  const std::span<const TraceConstants> constants_span(
      &trace_constants, 1);
  auto constant_buffer = gpu::GpuBuffer::Upload(
      context, std::as_bytes(constants_span));

  gpu::DescriptorArena descriptors(context, 1);
  D3D12_UNORDERED_ACCESS_VIEW_DESC visibility_uav{};
  visibility_uav.Format = DXGI_FORMAT_R32_UINT;
  visibility_uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  visibility_uav.Buffer.NumElements =
      static_cast<UINT>(word_count);
  context.device()->CreateUnorderedAccessView(
      visibility_buffer.resource(), nullptr, &visibility_uav,
      descriptors.cpu_handle(0));

  auto* commands = context.BeginCommands();
  auto to_uav = TransitionBarrier(
      visibility_buffer.resource(), D3D12_RESOURCE_STATE_COMMON,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  commands->ResourceBarrier(1, &to_uav);
  ID3D12DescriptorHeap* descriptor_heaps[] = {
      descriptors.heap()};
  commands->SetDescriptorHeaps(1, descriptor_heaps);
  const std::array<UINT, 4> zero{};
  commands->ClearUnorderedAccessViewUint(
      descriptors.gpu_handle(0), descriptors.cpu_handle(0),
      visibility_buffer.resource(), zero.data(), 0, nullptr);
  auto clear_barrier = UavBarrier(visibility_buffer.resource());
  commands->ResourceBarrier(1, &clear_barrier);

  commands->SetComputeRootSignature(root_signature);
  commands->SetPipelineState1(state_object);
  commands->SetComputeRootShaderResourceView(
      0, scene.tlas_result->GetGPUVirtualAddress());
  commands->SetComputeRootShaderResourceView(
      1, probe_buffer.gpu_address());
  commands->SetComputeRootDescriptorTable(
      2, descriptors.gpu_handle(0));
  commands->SetComputeRootConstantBufferView(
      3, constant_buffer.gpu_address());

  D3D12_DISPATCH_RAYS_DESC rays{};
  const std::uint64_t shader_table_address =
      shader_table.gpu_address();
  rays.RayGenerationShaderRecord.StartAddress =
      shader_table_address;
  rays.RayGenerationShaderRecord.SizeInBytes =
      kShaderRecordSize;
  rays.MissShaderTable.StartAddress =
      shader_table_address + kShaderRecordSize;
  rays.MissShaderTable.SizeInBytes = kShaderRecordSize;
  rays.MissShaderTable.StrideInBytes = kShaderRecordSize;
  rays.HitGroupTable.StartAddress =
      shader_table_address + 2u * kShaderRecordSize;
  rays.HitGroupTable.SizeInBytes = kShaderRecordSize;
  rays.HitGroupTable.StrideInBytes = kShaderRecordSize;
  rays.Width = dispatch.width;
  rays.Height = dispatch.height;
  rays.Depth = dispatch.depth;
  commands->DispatchRays(&rays);

  auto dispatch_barrier =
      UavBarrier(visibility_buffer.resource());
  commands->ResourceBarrier(1, &dispatch_barrier);
  auto to_copy = TransitionBarrier(
      visibility_buffer.resource(),
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);
  commands->ResourceBarrier(1, &to_copy);
  commands->CopyBufferRegion(
      visibility_readback.resource(), 0,
      visibility_buffer.resource(), 0, byte_count);
  context.ExecuteAndWait();

  return VisibilityTraceBatch{
      first_probe, batch_count,
      ReadVisibilityWords(visibility_readback, word_count)};
}

}  // namespace

namespace detail {

void ValidateTraceInstanceCount(std::uint64_t object_count) {
  if (object_count >
      D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE) {
    throw std::length_error(
        "World object count exceeds the DXR TLAS instance limit.");
  }
}

void ValidateTraceInstanceSlot(world::InstanceSlot object_slot) {
  if (object_slot >=
      D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE) {
    throw std::out_of_range(
        "Object slot exceeds the DXR InstanceID range.");
  }
}

void ValidateBlasPrimitiveCount(std::uint64_t primitive_count) {
  if (primitive_count >
      D3D12_RAYTRACING_MAX_PRIMITIVES_PER_BOTTOM_LEVEL_ACCELERATION_STRUCTURE) {
    throw std::length_error(
        "Mesh primitive count exceeds the DXR BLAS limit.");
  }
}

std::uint32_t MaximumTraceProbeBatch(
    std::uint32_t face_resolution) {
  const std::uint64_t maximum =
      kMaximumRayDispatchDimension / RaysPerProbe(face_resolution);
  return static_cast<std::uint32_t>(maximum);
}

TraceDispatchDimensions CheckedTraceDispatchDimensions(
    std::uint32_t face_resolution, std::uint32_t batch_probe_count) {
  if (batch_probe_count == 0) {
    throw std::invalid_argument(
        "Trace probe batch must contain at least one probe.");
  }
  const std::uint64_t rays_per_probe =
      RaysPerProbe(face_resolution);
  const std::uint32_t maximum_batch =
      MaximumTraceProbeBatch(face_resolution);
  if (batch_probe_count > maximum_batch) {
    throw std::length_error(
        "Trace probe batch exceeds the DXR ray-dispatch limit.");
  }
  const std::uint64_t width =
      static_cast<std::uint64_t>(face_resolution) * face_resolution;
  return TraceDispatchDimensions{
      static_cast<std::uint32_t>(width),
      kCubeFaceCount,
      batch_probe_count,
      rays_per_probe * batch_probe_count};
}

}  // namespace detail

struct DxrTraceProgram::Impl {
  explicit Impl(gpu::GpuContext& gpu_context)
      : context(&gpu_context),
        root_signature(CreateRootSignature(gpu_context)) {
    const auto shader = ReadShader(
        std::filesystem::path(PARALLAX_FORGE_PVS_TRACE_DXIL));
    state_object = CreateStateObject(
        gpu_context, root_signature.Get(), shader);
    shader_table.emplace(
        CreateShaderTable(gpu_context, state_object.Get()));
  }

  gpu::GpuContext* context;
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12StateObject> state_object;
  std::optional<gpu::GpuBuffer> shader_table;
};

DxrTraceProgram::DxrTraceProgram(gpu::GpuContext& context)
    : impl_(std::make_unique<Impl>(context)) {}

DxrTraceProgram::~DxrTraceProgram() = default;
DxrTraceProgram::DxrTraceProgram(DxrTraceProgram&&) noexcept = default;
DxrTraceProgram& DxrTraceProgram::operator=(
    DxrTraceProgram&&) noexcept = default;

std::vector<VisibilityTraceBatch> DxrTraceProgram::Trace(
    const world::WorldModel& world_model,
    std::span<const world::Vec3> probes,
    const world::TraceSettings& settings) {
  if (impl_ == nullptr || impl_->context == nullptr ||
      !impl_->shader_table.has_value()) {
    throw std::logic_error(
        "Cannot trace with a moved-from DxrTraceProgram.");
  }
  if (probes.empty()) {
    return {};
  }
  if (probes.size() >
      (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "Probe count exceeds the public probe-ID range.");
  }
  if (!std::isfinite(settings.max_distance) ||
      settings.max_distance <= 0.0f) {
    throw std::invalid_argument(
        "Trace maximum distance must be positive and finite.");
  }
  const std::uint32_t ray_dispatch_batch =
      detail::MaximumTraceProbeBatch(settings.face_resolution);
  const std::uint32_t visibility_batch =
      MaximumVisibilityProbeBatch(world_model.objects.size());
  const std::uint32_t maximum_batch =
      (std::min)(ray_dispatch_batch, visibility_batch);
  if (maximum_batch == 0) {
    throw std::length_error(
        "Trace resolution leaves no room for one probe.");
  }

  const auto object_count =
      static_cast<std::uint64_t>(world_model.objects.size());
  if (object_count >
      (std::numeric_limits<std::uint32_t>::max)()) {
    throw std::length_error(
        "World object count exceeds the shader range.");
  }
  SceneResources scene = BuildScene(*impl_->context, world_model);

  std::vector<VisibilityTraceBatch> batches;
  std::uint32_t first_probe = 0;
  const auto total_probe_count =
      static_cast<std::uint32_t>(probes.size());
  while (first_probe < total_probe_count) {
    const std::uint32_t remaining =
        total_probe_count - first_probe;
    const std::uint32_t batch_count =
        (std::min)(remaining, maximum_batch);
    batches.push_back(DispatchBatch(
        *impl_->context, impl_->root_signature.Get(),
        impl_->state_object.Get(), *impl_->shader_table, scene,
        first_probe, probes.subspan(first_probe, batch_count),
        settings, static_cast<std::uint32_t>(object_count)));
    first_probe += batch_count;
  }
  return batches;
}

}  // namespace parallax_forge::tracing
