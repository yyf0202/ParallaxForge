#pragma once

#include <d3d12.h>

#include <memory>

namespace parallax_forge::gpu {

class GpuContext {
 public:
  static GpuContext Create();

  GpuContext(GpuContext&&) noexcept;
  GpuContext& operator=(GpuContext&&) noexcept;
  GpuContext(const GpuContext&) = delete;
  GpuContext& operator=(const GpuContext&) = delete;
  ~GpuContext();

  [[nodiscard]] bool SupportsDxr() const noexcept;
  [[nodiscard]] ID3D12Device5* device() const noexcept;
  [[nodiscard]] ID3D12GraphicsCommandList4* BeginCommands();
  void ExecuteAndWait();

 private:
  struct Impl;

  explicit GpuContext(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

}  // namespace parallax_forge::gpu
