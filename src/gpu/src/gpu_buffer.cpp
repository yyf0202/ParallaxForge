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

GpuBuffer::GpuBuffer(ComPtr<ID3D12Resource> resource,
                     std::uint64_t byte_size) noexcept
    : resource_(std::move(resource)), byte_size_(byte_size) {}

GpuBuffer GpuBuffer::DefaultUav(GpuContext& context, std::uint64_t bytes) {
  return GpuBuffer(
      CreateBuffer(context, bytes, D3D12_HEAP_TYPE_DEFAULT,
                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COMMON),
      bytes);
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
                   static_cast<std::uint64_t>(bytes.size()));
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
