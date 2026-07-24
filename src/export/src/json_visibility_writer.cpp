#include <parallax_forge/export/json_visibility_writer.hpp>

#include "transactional_output.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace parallax_forge::export_data {
namespace {

nlohmann::json PositionJson(const world::Vec3& position) {
  return nlohmann::json::array({position.x, position.y, position.z});
}

}  // namespace

void WriteJsonVisibility(const VisibilityCatalog& catalog, const std::filesystem::path& output_path) {
  nlohmann::json objects = nlohmann::json::array();
  for (const auto& object : catalog.Objects()) {
    objects.push_back({{"object_id", object.object_id}, {"label", object.label}});
  }

  nlohmann::json probes = nlohmann::json::array();
  for (const auto& probe : catalog.Probes()) {
    probes.push_back({
        {"probe_id", probe.probe_id},
        {"position", PositionJson(probe.position)},
        {"visible_object_ids", probe.visible_object_ids},
    });
  }

  const nlohmann::json document = {
      {"format", "parallax-forge.visibility"},
      {"version", 1},
      {"objects", std::move(objects)},
      {"probes", std::move(probes)},
  };

  detail::TransactionalOutput output(output_path);
  std::ofstream file(output.TemporaryPath(), std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("failed to open temporary visibility JSON output");
  }
  file << document.dump(2) << '\n';
  if (!file) {
    throw std::runtime_error("failed to write temporary visibility JSON output");
  }
  file.close();
  if (!file) {
    throw std::runtime_error("failed to close temporary visibility JSON output");
  }
  output.Commit();
}

}  // namespace parallax_forge::export_data
