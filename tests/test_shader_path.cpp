#include "parallax_forge/gpu/shader_path.hpp"

#include <cassert>
#include <filesystem>

int main() {
  using parallax_forge::gpu::ShaderOutputFile;

  const auto output =
      ShaderOutputFile("generated/shaders",
                       "src/voxel/shaders/volume_rasterizer.hlsl");

  assert(output ==
         std::filesystem::path("generated/shaders/volume_rasterizer.dxil"));
}
