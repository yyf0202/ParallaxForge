#pragma once

#include <filesystem>

#include <parallax_forge/export/visibility_catalog.hpp>

namespace parallax_forge::export_data {

void WriteVisibilityOutputPair(
    const VisibilityCatalog& catalog,
    const std::filesystem::path& json_path,
    const std::filesystem::path& pfvis_path);

}  // namespace parallax_forge::export_data
