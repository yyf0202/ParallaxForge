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
  static GpuBuffer Readback(GpuContext& context, std::uint64_t bytes);

  GpuBuffer(GpuBuffer&& other) noexcept;
  GpuBuffer& operator=(GpuBuffer&& other) noexcept;
  GpuBuffer(const GpuBuffer&) = delete;
  GpuBuffer& operator=(const GpuBuffer&) = delete;
  ~GpuBuffer() = default;

  void CopyToReadback(GpuContext& context, GpuBuffer& destination,
                      D3D12_RESOURCE_STATES source_state) const;
  [[nodiscard]] ID3D12Resource* resource() const noexcept;
  [[nodiscard]] std::uint64_t byte_size() const noexcept;
  [[nodiscard]] std::uint64_t gpu_address() const noexcept;

 private:
  enum class Storage : std::uint8_t {
    None,
    DefaultUav,
    Upload,
    Readback,
  };

  GpuBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource,
            std::uint64_t byte_size, Storage storage) noexcept;

  Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
  std::uint64_t byte_size_ = 0;
  Storage storage_ = Storage::None;
};

}  // namespace parallax_forge::gpu
