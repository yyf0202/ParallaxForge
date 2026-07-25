#pragma once

#include <filesystem>

namespace parallax_forge::gpu {

[[nodiscard]] std::filesystem::path ShaderOutputFile(
    const std::filesystem::path& output_directory,
    const std::filesystem::path& source_file);

}  // namespace parallax_forge::gpu
