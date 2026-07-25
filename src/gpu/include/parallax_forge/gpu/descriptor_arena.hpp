#pragma once

#include "parallax_forge/gpu/gpu_context.hpp"

#include <cstdint>
#include <memory>

namespace parallax_forge::gpu {

class DescriptorArena {
 public:
  explicit DescriptorArena(GpuContext& context, std::uint32_t capacity);

  DescriptorArena(DescriptorArena&&) noexcept;
  DescriptorArena& operator=(DescriptorArena&&) noexcept;
  DescriptorArena(const DescriptorArena&) = delete;
  DescriptorArena& operator=(const DescriptorArena&) = delete;
  ~DescriptorArena();

  [[nodiscard]] ID3D12DescriptorHeap* heap() const noexcept;
  [[nodiscard]] std::uint32_t capacity() const noexcept;
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle(
      std::uint32_t index) const;
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle(
      std::uint32_t index) const;

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace parallax_forge::gpu
