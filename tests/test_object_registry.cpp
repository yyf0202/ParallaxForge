#include <cassert>
#include <stdexcept>

#include <parallax_forge/world/object_registry.hpp>

int main() {
  using namespace parallax_forge::world;
  ObjectRegistry registry({
      ObjectDefinition{100u, "floor", "meshes/floor.obj", Transform::Identity()},
      ObjectDefinition{200u, "central_block", "meshes/block.obj", Transform::Identity()},
  });

  assert(registry.SlotFor(100u).value() == 0u);
  assert(registry.SlotFor(200u).value() == 1u);
  assert(registry.ObjectFor(0u).object_id == 100u);
  assert(!registry.SlotFor(999u).has_value());

  bool duplicate_id_rejected = false;
  try {
    ObjectRegistry({
        ObjectDefinition{100u, "floor", "meshes/floor.obj", Transform::Identity()},
        ObjectDefinition{100u, "central_block", "meshes/block.obj", Transform::Identity()},
    });
  } catch (const std::invalid_argument&) {
    duplicate_id_rejected = true;
  }
  assert(duplicate_id_rejected);

  ObjectRegistry unsorted_registry({
      ObjectDefinition{200u, "central_block", "meshes/block.obj", Transform::Identity()},
      ObjectDefinition{100u, "floor", "meshes/floor.obj", Transform::Identity()},
  });
  assert(unsorted_registry.SlotFor(100u).value() == 0u);
  assert(unsorted_registry.SlotFor(200u).value() == 1u);
  assert(unsorted_registry.Objects()[0].object_id == 100u);
  assert(unsorted_registry.Objects()[1].object_id == 200u);

  bool zero_id_rejected = false;
  try {
    ObjectRegistry({
        ObjectDefinition{0u, "invalid", "meshes/invalid.obj", Transform::Identity()},
    });
  } catch (const std::invalid_argument&) {
    zero_id_rejected = true;
  }
  assert(zero_id_rejected);

  bool invalid_slot_rejected = false;
  try {
    static_cast<void>(registry.ObjectFor(2u));
  } catch (const std::out_of_range&) {
    invalid_slot_rejected = true;
  }
  assert(invalid_slot_rejected);
  return 0;
}
