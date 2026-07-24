#include <parallax_forge/export/visibility_catalog.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace parallax_forge::export_data {

VisibilityCatalog::VisibilityCatalog(std::vector<world::ObjectDefinition> objects)
    : objects_(std::move(objects)) {
  std::sort(objects_.begin(), objects_.end(), [](const auto& left, const auto& right) {
    return left.object_id < right.object_id;
  });

  const auto duplicate = std::adjacent_find(
      objects_.begin(), objects_.end(), [](const auto& left, const auto& right) {
        return left.object_id == right.object_id;
      });
  if (duplicate != objects_.end()) {
    throw std::invalid_argument("object_id must be unique");
  }
}

void VisibilityCatalog::AddProbe(ProbeVisibility probe) {
  if (!std::isfinite(probe.position.x) || !std::isfinite(probe.position.y) ||
      !std::isfinite(probe.position.z)) {
    throw std::invalid_argument("probe position must be finite");
  }

  std::sort(probe.visible_object_ids.begin(), probe.visible_object_ids.end());
  probe.visible_object_ids.erase(
      std::unique(probe.visible_object_ids.begin(), probe.visible_object_ids.end()),
      probe.visible_object_ids.end());

  for (const auto object_id : probe.visible_object_ids) {
    const auto object = std::lower_bound(
        objects_.begin(), objects_.end(), object_id,
        [](const world::ObjectDefinition& definition, world::ObjectId id) {
          return definition.object_id < id;
        });
    if (object == objects_.end() || object->object_id != object_id) {
      throw std::invalid_argument("visible object_id is not in the catalog");
    }
  }

  const auto insertion_point = std::lower_bound(
      probes_.begin(), probes_.end(), probe.probe_id,
      [](const ProbeVisibility& existing, ProbeId id) { return existing.probe_id < id; });
  if (insertion_point != probes_.end() && insertion_point->probe_id == probe.probe_id) {
    throw std::invalid_argument("probe_id must be unique");
  }
  probes_.insert(insertion_point, std::move(probe));
}

const std::vector<world::ObjectDefinition>& VisibilityCatalog::Objects() const noexcept {
  return objects_;
}

const std::vector<ProbeVisibility>& VisibilityCatalog::Probes() const noexcept {
  return probes_;
}

}  // namespace parallax_forge::export_data
