#include "gpu_context_features.hpp"

#include <cstdio>
#include <cstdlib>

#undef assert
#define assert(expression)                                                                      \
  do {                                                                                          \
    if (!(expression)) {                                                                        \
      std::fprintf(stderr, "assertion failed: %s (%s:%d)\n", #expression, __FILE__, __LINE__); \
      std::_Exit(1);                                                                             \
    }                                                                                           \
  } while (false)

int main() {
  using parallax_forge::gpu::detail::SupportsRequiredGpuFeatures;

  assert(!SupportsRequiredGpuFeatures(
      D3D12_RAYTRACING_TIER_NOT_SUPPORTED));
  assert(SupportsRequiredGpuFeatures(
      D3D12_RAYTRACING_TIER_1_0));
  assert(SupportsRequiredGpuFeatures(
      D3D12_RAYTRACING_TIER_1_1));
  return 0;
}
