#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tiny_obj_loader.h>

#include <parallax_forge/world/obj_world_importer.hpp>
#include <parallax_forge/world/object_registry.hpp>

namespace parallax_forge::world {
namespace {
[[noreturn]] void ImportError(const ObjectDefinition& object, const std::string& message) {
  throw std::runtime_error(
      "world import: mesh '" + object.mesh_path + "' " + message);
}

Vec3 ReadVertex(
    const tinyobj::attrib_t& attributes, const tinyobj::index_t& index,
    const ObjectDefinition& object) {
  if (index.vertex_index < 0) {
    ImportError(object, "contains a face without a position");
  }
  const auto vertex_index = static_cast<std::size_t>(index.vertex_index);
  if (vertex_index > (std::numeric_limits<std::size_t>::max() - 2u) / 3u) {
    ImportError(object, "contains an out-of-range position index");
  }
  const std::size_t offset = vertex_index * 3u;
  if (offset + 2u >= attributes.vertices.size()) {
    ImportError(object, "contains an out-of-range position index");
  }
  return Vec3{
      attributes.vertices[offset],
      attributes.vertices[offset + 1u],
      attributes.vertices[offset + 2u],
  };
}

std::vector<Triangle> ImportTriangles(
    const ObjectDefinition& object, const std::filesystem::path& config_directory) {
  tinyobj::attrib_t attributes;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warning;
  std::string error;
  const auto mesh_path = config_directory / object.mesh_path;
  if (!tinyobj::LoadObj(
          &attributes, &shapes, &materials, &warning, &error,
          mesh_path.string().c_str(), nullptr, true)) {
    ImportError(object, "could not be loaded: " + error);
  }

  std::vector<Triangle> triangles;
  for (const auto& shape : shapes) {
    std::size_t index_offset = 0u;
    for (const unsigned int vertex_count : shape.mesh.num_face_vertices) {
      if (vertex_count != 3u || index_offset + 3u > shape.mesh.indices.size()) {
        ImportError(object, "did not triangulate into valid faces");
      }
      triangles.push_back(Triangle{
          ReadVertex(attributes, shape.mesh.indices[index_offset], object),
          ReadVertex(attributes, shape.mesh.indices[index_offset + 1u], object),
          ReadVertex(attributes, shape.mesh.indices[index_offset + 2u], object),
      });
      index_offset += 3u;
    }
    if (index_offset != shape.mesh.indices.size()) {
      ImportError(object, "contains unused face indices");
    }
  }
  if (triangles.empty()) {
    ImportError(object, "yielded no triangles");
  }
  return triangles;
}
}  // namespace

WorldModel ImportWorld(const BakeConfig& config) {
  const ObjectRegistry registry(config.objects);
  if (registry.Objects().size() > std::numeric_limits<InstanceSlot>::max()) {
    throw std::runtime_error("world import: object count exceeds instance slot capacity");
  }

  std::vector<ImportedObject> objects;
  objects.reserve(registry.Objects().size());
  for (std::size_t index = 0; index < registry.Objects().size(); ++index) {
    const ObjectDefinition& object = registry.Objects()[index];
    objects.push_back(ImportedObject{
        object.object_id,
        static_cast<InstanceSlot>(index),
        object.transform,
        ImportTriangles(object, config.config_directory),
    });
  }
  return WorldModel{config.bounds, std::move(objects)};
}
}  // namespace parallax_forge::world
