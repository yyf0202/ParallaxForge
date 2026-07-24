#pragma once

#include <filesystem>

#include <parallax_forge/world/bake_config.hpp>

namespace parallax_forge::world {
[[nodiscard]] BakeConfig LoadBakeConfig(const std::filesystem::path& config_path);
}  // namespace parallax_forge::world
