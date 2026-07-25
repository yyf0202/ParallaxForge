#include "parallax_forge/gpu/descriptor_arena.hpp"

#include <wrl/client.h>

#include <stdexcept>
#include <utility>

namespace parallax_forge::gpu {
namespace {

using Microsoft::WRL::ComPtr;

}  // namespace

struct DescriptorArena::Impl {
  ComPtr<ID3D12DescriptorHeap> heap;
  std::uint32_t capacity = 0;
  std::uint32_t descriptor_size = 0;
};

DescriptorArena::DescriptorArena(GpuContext& context, std::uint32_t capacity)
    : impl_(std::make_unique<Impl>()) {
  if (capacity == 0) {
    throw std::invalid_argument(
        "Descriptor arena capacity must be greater than zero.");
  }
  if (context.device() == nullptr) {
    throw std::invalid_argument("GpuContext does not contain a device.");
  }

  D3D12_DESCRIPTOR_HEAP_DESC description{};
  description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  description.NumDescriptors = capacity;
  description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  description.NodeMask = 0;
  if (FAILED(context.device()->CreateDescriptorHeap(
          &description, IID_PPV_ARGS(&impl_->heap)))) {
    throw std::runtime_error("ID3D12Device::CreateDescriptorHeap failed.");
  }

  impl_->capacity = capacity;
  impl_->descriptor_size = context.device()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

DescriptorArena::DescriptorArena(DescriptorArena&&) noexcept = default;
DescriptorArena& DescriptorArena::operator=(DescriptorArena&&) noexcept =
    default;
DescriptorArena::~DescriptorArena() = default;

ID3D12DescriptorHeap* DescriptorArena::heap() const noexcept {
  return impl_ == nullptr ? nullptr : impl_->heap.Get();
}

std::uint32_t DescriptorArena::capacity() const noexcept {
  return impl_ == nullptr ? 0 : impl_->capacity;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorArena::cpu_handle(
    std::uint32_t index) const {
  if (impl_ == nullptr || index >= impl_->capacity) {
    throw std::out_of_range("Descriptor index is outside the arena.");
  }

  auto handle = impl_->heap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(index) * impl_->descriptor_size;
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorArena::gpu_handle(
    std::uint32_t index) const {
  if (impl_ == nullptr || index >= impl_->capacity) {
    throw std::out_of_range("Descriptor index is outside the arena.");
  }

  auto handle = impl_->heap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<UINT64>(index) * impl_->descriptor_size;
  return handle;
}

}  // namespace parallax_forge::gpu
