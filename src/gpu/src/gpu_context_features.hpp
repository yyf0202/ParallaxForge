#pragma once

#include <d3d12.h>

namespace parallax_forge::gpu::detail {

[[nodiscard]] bool SupportsRequiredGpuFeatures(
    D3D12_RAYTRACING_TIER raytracing_tier,
    bool int64_shader_ops) noexcept;

}  // namespace parallax_forge::gpu::detail
