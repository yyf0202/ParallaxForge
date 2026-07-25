#pragma once

#include <vector>

#include <parallax_forge/world/types.hpp>

namespace parallax_forge::world {
struct Triangle {
  Vec3 a;
  Vec3 b;
  Vec3 c;
};

struct ImportedObject {
  ObjectId object_id{};
  InstanceSlot slot{};
  Transform local_to_world;
  std::vector<Triangle> triangles;
};

struct WorldModel {
  Bounds bounds;
  std::vector<ImportedObject> objects;
};
}  // namespace parallax_forge::world
