#pragma once

#include <cstdint>
#include <vector>

#include <parallax_forge/world/types.hpp>

namespace parallax_forge::export_data {

using ProbeId = std::uint32_t;

struct ProbeVisibility {
  ProbeId probe_id{};
  world::Vec3 position;
  std::vector<world::ObjectId> visible_object_ids;
};

class VisibilityCatalog {
 public:
  explicit VisibilityCatalog(std::vector<world::ObjectDefinition> objects);

  void AddProbe(ProbeVisibility probe);

  [[nodiscard]] const std::vector<world::ObjectDefinition>& Objects() const noexcept;
  [[nodiscard]] const std::vector<ProbeVisibility>& Probes() const noexcept;

 private:
  std::vector<world::ObjectDefinition> objects_;
  std::vector<ProbeVisibility> probes_;
};

}  // namespace parallax_forge::export_data
