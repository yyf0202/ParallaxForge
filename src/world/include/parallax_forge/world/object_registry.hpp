#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <parallax_forge/world/types.hpp>

namespace parallax_forge::world {
class ObjectRegistry {
 public:
  explicit ObjectRegistry(std::vector<ObjectDefinition> objects);

  [[nodiscard]] std::optional<InstanceSlot> SlotFor(ObjectId object_id) const;
  [[nodiscard]] const ObjectDefinition& ObjectFor(InstanceSlot slot) const;
  [[nodiscard]] const std::vector<ObjectDefinition>& Objects() const noexcept;

 private:
  std::vector<ObjectDefinition> objects_;
  std::vector<std::pair<ObjectId, InstanceSlot>> slots_by_id_;
};
}  // namespace parallax_forge::world
