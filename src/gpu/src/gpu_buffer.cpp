#include "parallax_forge/gpu/gpu_buffer.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace parallax_forge::gpu {
namespace {

using Microsoft::WRL::ComPtr;

void ThrowIfFailed(HRESULT result, const char* operation) {
  if (FAILED(result)) {
    throw std::runtime_error(operation);
  }
}

ComPtr<ID3D12Resource> CreateBuffer(
    GpuContext& context, std::uint64_t bytes, D3D12_HEAP_TYPE heap_type,
    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initial_state) {
  if (bytes == 0) {
    throw std::invalid_argument("GPU buffer size must be greater than zero.");
  }
  if (context.device() == nullptr) {
    throw std::invalid_argument("GpuContext does not contain a device.");
  }

  D3D12_HEAP_PROPERTIES heap_properties{};
  heap_properties.Type = heap_type;
  heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_properties.CreationNodeMask = 1;
  heap_properties.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC description{};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Alignment = 0;
  description.Width = bytes;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.Format = DXGI_FORMAT_UNKNOWN;
  description.SampleDesc.Count = 1;
  description.SampleDesc.Quality = 0;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  description.Flags = flags;

  ComPtr<ID3D12Resource> resource;
  ThrowIfFailed(
      context.device()->CreateCommittedResource(
          &heap_properties, D3D12_HEAP_FLAG_NONE, &description, initial_state,
          nullptr, IID_PPV_ARGS(&resource)),
      "ID3D12Device::CreateCommittedResource failed.");
  return resource;
}

}  // namespace

GpuBuffer::GpuBuffer(ComPtr<ID3D12Resource> resource, std::uint64_t byte_size,
                     Storage storage) noexcept
    : resource_(std::move(resource)),
      byte_size_(byte_size),
      storage_(storage) {}

GpuBuffer GpuBuffer::DefaultUav(GpuContext& context, std::uint64_t bytes) {
  return GpuBuffer(
      CreateBuffer(context, bytes, D3D12_HEAP_TYPE_DEFAULT,
                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COMMON),
      bytes, Storage::DefaultUav);
}

GpuBuffer GpuBuffer::Upload(GpuContext& context,
                            std::span<const std::byte> bytes) {
  if (bytes.size() >
      static_cast<std::size_t>((std::numeric_limits<SIZE_T>::max)())) {
    throw std::length_error("Upload buffer contents exceed addressable memory.");
  }

  auto resource =
      CreateBuffer(context, static_cast<std::uint64_t>(bytes.size()),
                   D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                   D3D12_RESOURCE_STATE_GENERIC_READ);
  void* mapped_data = nullptr;
  const D3D12_RANGE read_range{0, 0};
  ThrowIfFailed(resource->Map(0, &read_range, &mapped_data),
                "ID3D12Resource::Map failed.");
  std::memcpy(mapped_data, bytes.data(), bytes.size());
  const D3D12_RANGE written_range{0, bytes.size()};
  resource->Unmap(0, &written_range);

  return GpuBuffer(std::move(resource),
                   static_cast<std::uint64_t>(bytes.size()), Storage::Upload);
}

GpuBuffer GpuBuffer::Readback(GpuContext& context, std::uint64_t bytes) {
  return GpuBuffer(
      CreateBuffer(context, bytes, D3D12_HEAP_TYPE_READBACK,
                   D3D12_RESOURCE_FLAG_NONE,
                   D3D12_RESOURCE_STATE_COPY_DEST),
      bytes, Storage::Readback);
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : resource_(std::move(other.resource_)),
      byte_size_(std::exchange(other.byte_size_, 0)),
      storage_(std::exchange(other.storage_, Storage::None)) {}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
  if (this != &other) {
    resource_ = std::move(other.resource_);
    byte_size_ = std::exchange(other.byte_size_, 0);
    storage_ = std::exchange(other.storage_, Storage::None);
  }
  return *this;
}

void GpuBuffer::CopyToReadback(
    GpuContext& context, GpuBuffer& destination,
    D3D12_RESOURCE_STATES source_state) const {
  if (storage_ != Storage::DefaultUav || resource_ == nullptr) {
    throw std::logic_error(
        "Only a default UAV buffer can be copied to readback.");
  }
  if (destination.storage_ != Storage::Readback ||
      destination.resource_ == nullptr) {
    throw std::invalid_argument(
        "Copy destination must be a readback buffer.");
  }
  if (destination.byte_size_ < byte_size_) {
    throw std::invalid_argument(
        "Readback buffer is smaller than the source buffer.");
  }

  auto* command_list = context.BeginCommands();
  if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    D3D12_RESOURCE_BARRIER to_copy_source{};
    to_copy_source.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy_source.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    to_copy_source.Transition.pResource = resource_.Get();
    to_copy_source.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    to_copy_source.Transition.StateBefore = source_state;
    to_copy_source.Transition.StateAfter =
        D3D12_RESOURCE_STATE_COPY_SOURCE;
    command_list->ResourceBarrier(1, &to_copy_source);
  }

  command_list->CopyBufferRegion(destination.resource_.Get(), 0,
                                 resource_.Get(), 0, byte_size_);

  if (source_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    D3D12_RESOURCE_BARRIER restore_source{};
    restore_source.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    restore_source.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    restore_source.Transition.pResource = resource_.Get();
    restore_source.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    restore_source.Transition.StateBefore =
        D3D12_RESOURCE_STATE_COPY_SOURCE;
    restore_source.Transition.StateAfter = source_state;
    command_list->ResourceBarrier(1, &restore_source);
  }

  context.ExecuteAndWait();
}

ID3D12Resource* GpuBuffer::resource() const noexcept {
  return resource_.Get();
}

std::uint64_t GpuBuffer::byte_size() const noexcept {
  return byte_size_;
}

std::uint64_t GpuBuffer::gpu_address() const noexcept {
  return resource_ == nullptr ? 0 : resource_->GetGPUVirtualAddress();
}

}  // namespace parallax_forge::gpu
