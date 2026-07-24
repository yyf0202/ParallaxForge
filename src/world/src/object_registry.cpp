#include <algorithm>
#include <stdexcept>
#include <utility>

#include <parallax_forge/world/object_registry.hpp>

namespace parallax_forge::world {
ObjectRegistry::ObjectRegistry(std::vector<ObjectDefinition> objects)
    : objects_(std::move(objects)) {
  std::sort(objects_.begin(), objects_.end(), [](const auto& left, const auto& right) {
    return left.object_id < right.object_id;
  });

  slots_by_id_.reserve(objects_.size());
  for (InstanceSlot slot = 0; slot < objects_.size(); ++slot) {
    const auto object_id = objects_[slot].object_id;
    if (object_id == 0u || (!slots_by_id_.empty() && slots_by_id_.back().first == object_id)) {
      throw std::invalid_argument("object_id must be nonzero and unique");
    }
    slots_by_id_.emplace_back(object_id, slot);
  }
}

std::optional<InstanceSlot> ObjectRegistry::SlotFor(ObjectId object_id) const {
  const auto found = std::lower_bound(
      slots_by_id_.begin(), slots_by_id_.end(), object_id,
      [](const auto& entry, ObjectId value) { return entry.first < value; });
  return found != slots_by_id_.end() && found->first == object_id
      ? std::optional<InstanceSlot>{found->second}
      : std::nullopt;
}

const ObjectDefinition& ObjectRegistry::ObjectFor(InstanceSlot slot) const {
  if (slot >= objects_.size()) {
    throw std::out_of_range("instance slot is outside the object registry");
  }
  return objects_[slot];
}

const std::vector<ObjectDefinition>& ObjectRegistry::Objects() const noexcept {
  return objects_;
}
}  // namespace parallax_forge::world
