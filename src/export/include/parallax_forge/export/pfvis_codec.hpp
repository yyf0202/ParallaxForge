#pragma once

#include <filesystem>

#include <parallax_forge/export/visibility_catalog.hpp>

namespace parallax_forge::export_data {

void WritePfvis(const VisibilityCatalog& catalog, const std::filesystem::path& output_path);
[[nodiscard]] VisibilityCatalog ReadPfvis(const std::filesystem::path& input_path);

}  // namespace parallax_forge::export_data
