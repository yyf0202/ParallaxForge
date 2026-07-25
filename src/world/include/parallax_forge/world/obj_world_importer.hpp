#pragma once

#include <parallax_forge/world/bake_config.hpp>
#include <parallax_forge/world/world_model.hpp>

namespace parallax_forge::world {
[[nodiscard]] WorldModel ImportWorld(const BakeConfig& config);
}  // namespace parallax_forge::world
