#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <parallax_forge/world/config_loader.hpp>
#include <parallax_forge/world/object_registry.hpp>

namespace parallax_forge::world {
namespace {
using Json = nlohmann::json;

[[noreturn]] void ConfigurationError(const std::string& message) {
  throw std::runtime_error("configuration: " + message);
}

const Json& Required(const Json& object, const char* name) {
  if (!object.is_object() || !object.contains(name)) {
    ConfigurationError(std::string("missing required field '") + name + "'");
  }
  return object.at(name);
}

float Number(const Json& value, const char* description) {
  if (!value.is_number()) {
    ConfigurationError(std::string("'") + description + "' must be numeric");
  }
  const double number = value.get<double>();
  if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
      number > std::numeric_limits<float>::max()) {
    ConfigurationError(std::string("'") + description + "' must be finite");
  }
  return static_cast<float>(number);
}

float RequiredNumber(const Json& object, const char* name) {
  return Number(Required(object, name), name);
}

bool RequiredBool(const Json& object, const char* name) {
  const Json& value = Required(object, name);
  if (!value.is_boolean()) {
    ConfigurationError(std::string("'") + name + "' must be boolean");
  }
  return value.get<bool>();
}

std::string RequiredString(const Json& object, const char* name) {
  const Json& value = Required(object, name);
  if (!value.is_string()) {
    ConfigurationError(std::string("'") + name + "' must be a string");
  }
  return value.get<std::string>();
}

std::string OptionalString(const Json& object, const char* name) {
  if (!object.contains(name)) {
    return {};
  }
  const Json& value = object.at(name);
  if (!value.is_string()) {
    ConfigurationError(std::string("'") + name + "' must be a string");
  }
  return value.get<std::string>();
}

void RequireContainedRelativePath(
    const std::filesystem::path& path, const char* error_message) {
  const auto normalized = path.lexically_normal();
  if (path.has_root_name() || path.has_root_directory() ||
      (normalized.begin() != normalized.end() && *normalized.begin() == "..")) {
    ConfigurationError(error_message);
  }
}

Vec3 ParseVec3(const Json& value, const char* description) {
  if (!value.is_array() || value.size() != 3u) {
    ConfigurationError(std::string("'") + description + "' must contain exactly three numbers");
  }
  return Vec3{Number(value[0], description), Number(value[1], description), Number(value[2], description)};
}

ObjectId ParseObjectId(const Json& value) {
  if (!value.is_number_unsigned()) {
    ConfigurationError("'object_id' must be an unsigned integer");
  }
  const auto id = value.get<std::uint64_t>();
  if (id > std::numeric_limits<ObjectId>::max()) {
    ConfigurationError("'object_id' is out of range");
  }
  return static_cast<ObjectId>(id);
}

Transform ParseTransform(const Json& object) {
  if (!object.contains("transform")) {
    return Transform::Identity();
  }
  const Json& values = object.at("transform");
  if (!values.is_array() || values.size() != 16u) {
    ConfigurationError("'transform' must contain exactly sixteen numbers");
  }
  Transform transform{};
  for (std::size_t index = 0; index < transform.values.size(); ++index) {
    transform.values[index] = Number(values[index], "transform");
  }
  return transform;
}

ObjectDefinition ParseObject(const Json& object, const std::filesystem::path& config_directory) {
  if (!object.is_object()) {
    ConfigurationError("every world object must be an object");
  }
  const std::filesystem::path mesh_path(RequiredString(object, "mesh"));
  RequireContainedRelativePath(mesh_path, "mesh paths must be relative");
  if (!std::filesystem::is_regular_file(config_directory / mesh_path)) {
    ConfigurationError("mesh file does not exist: " + mesh_path.string());
  }
  return ObjectDefinition{
      ParseObjectId(Required(object, "object_id")),
      OptionalString(object, "label"),
      mesh_path.string(),
      ParseTransform(object),
  };
}
}  // namespace

BakeConfig LoadBakeConfig(const std::filesystem::path& config_path) {
  try {
    std::ifstream stream(config_path);
    if (!stream.is_open()) {
      ConfigurationError("cannot open file: " + config_path.string());
    }

    const Json root = Json::parse(stream);
    const Json& world = Required(root, "world");
    const Json& bounds = Required(world, "bounds");
    const Bounds parsed_bounds{
        ParseVec3(Required(bounds, "min"), "bounds.min"),
        ParseVec3(Required(bounds, "max"), "bounds.max"),
    };
    if (parsed_bounds.min.x >= parsed_bounds.max.x || parsed_bounds.min.y >= parsed_bounds.max.y ||
        parsed_bounds.min.z >= parsed_bounds.max.z) {
      ConfigurationError("every bounds minimum must be less than its maximum");
    }

    const Json& object_values = Required(world, "objects");
    if (!object_values.is_array()) {
      ConfigurationError("'objects' must be an array");
    }
    std::vector<ObjectDefinition> objects;
    objects.reserve(object_values.size());
    for (const Json& object : object_values) {
      objects.push_back(ParseObject(object, config_path.parent_path()));
    }
    try {
      ObjectRegistry registry(objects);
    } catch (const std::exception& error) {
      ConfigurationError(error.what());
    }

    const Json& voxel = Required(root, "voxel");
    const float voxel_size = RequiredNumber(voxel, "size");
    if (voxel_size <= 0.0f) {
      ConfigurationError("'voxel.size' must be positive");
    }

    const Json& probes = Required(root, "probes");
    const float probe_spacing = RequiredNumber(probes, "spacing");
    if (probe_spacing <= 0.0f) {
      ConfigurationError("'probes.spacing' must be positive");
    }

    const Json& trace = Required(root, "trace");
    const Json& face_resolution = Required(trace, "face_resolution");
    if (!face_resolution.is_number_unsigned() || face_resolution.get<std::uint64_t>() == 0u ||
        face_resolution.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
      ConfigurationError("'trace.face_resolution' must be a positive unsigned integer");
    }
    const float max_distance = RequiredNumber(trace, "max_distance");
    if (max_distance <= 0.0f) {
      ConfigurationError("'trace.max_distance' must be positive");
    }

    const Json& output = Required(root, "output");
    const std::filesystem::path output_directory(RequiredString(output, "directory"));
    RequireContainedRelativePath(output_directory, "output directory must be relative");

    return BakeConfig{
        parsed_bounds,
        std::move(objects),
        VoxelSettings{voxel_size, RequiredNumber(voxel, "clearance")},
        ProbeSettings{probe_spacing},
        TraceSettings{face_resolution.get<std::uint32_t>(), max_distance},
        OutputSettings{output_directory, RequiredBool(output, "write_json"), RequiredBool(output, "write_binary")},
    };
  } catch (const std::runtime_error& error) {
    if (std::string(error.what()).starts_with("configuration:")) {
      throw;
    }
    ConfigurationError(error.what());
  } catch (const std::exception& error) {
    ConfigurationError(error.what());
  }
}
}  // namespace parallax_forge::world
