#pragma once

#include <filesystem>

#include <parallax_forge/export/visibility_catalog.hpp>

namespace parallax_forge::export_data {

void WriteJsonVisibility(const VisibilityCatalog& catalog, const std::filesystem::path& output_path);

}  // namespace parallax_forge::export_data
