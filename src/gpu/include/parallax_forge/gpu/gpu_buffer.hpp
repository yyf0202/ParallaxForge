#pragma once

#include "parallax_forge/gpu/gpu_context.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include <wrl/client.h>

namespace parallax_forge::gpu {

class GpuBuffer {
 public:
  static GpuBuffer DefaultUav(GpuContext& context, std::uint64_t bytes);
  static GpuBuffer Upload(GpuContext& context,
                          std::span<const std::byte> bytes);

  GpuBuffer(GpuBuffer&&) noexcept = default;
  GpuBuffer& operator=(GpuBuffer&&) noexcept = default;
  GpuBuffer(const GpuBuffer&) = delete;
  GpuBuffer& operator=(const GpuBuffer&) = delete;
  ~GpuBuffer() = default;

  [[nodiscard]] ID3D12Resource* resource() const noexcept;
  [[nodiscard]] std::uint64_t byte_size() const noexcept;
  [[nodiscard]] std::uint64_t gpu_address() const noexcept;

 private:
  GpuBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
            std::uint64_t byte_size) noexcept;

  Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
  std::uint64_t byte_size_ = 0;
};

}  // namespace parallax_forge::gpu
