#include "parallax_forge/gpu/shader_path.hpp"

namespace parallax_forge::gpu {

std::filesystem::path ShaderOutputFile(
    const std::filesystem::path& output_directory,
    const std::filesystem::path& source_file) {
  return output_directory / source_file.stem().replace_extension(".dxil");
}

}  // namespace parallax_forge::gpu
