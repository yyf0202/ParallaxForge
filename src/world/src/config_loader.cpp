#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define NOMINMAX
#include <Windows.h>

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
    const std::filesystem::path& root,
    const std::filesystem::path& path,
    const char* error_message,
    const char* reparse_error_message) {
  const auto normalized = path.lexically_normal();
  if (path.has_root_name() || path.has_root_directory() ||
      (normalized.begin() != normalized.end() && *normalized.begin() == "..")) {
    ConfigurationError(error_message);
  }

  std::filesystem::path candidate = root;
  for (const auto& component : normalized) {
    if (component == ".") {
      continue;
    }
    candidate /= component;
    const DWORD attributes = GetFileAttributesW(candidate.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      ConfigurationError(reparse_error_message);
    }
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

Transform ParseTransform(const Json& values, const std::string& description) {
  if (!values.is_array() || values.size() != 16u) {
    ConfigurationError("'" + description + "' must contain exactly sixteen numbers");
  }
  Transform transform{};
  for (std::size_t index = 0; index < transform.values.size(); ++index) {
    transform.values[index] = Number(values[index], description.c_str());
  }
  return transform;
}

Transform ParseOptionalTransform(const Json& object) {
  if (!object.contains("transform")) {
    return Transform::Identity();
  }
  return ParseTransform(object.at("transform"), "transform");
}

bool IsAffine(const Transform& transform) noexcept {
  return transform.values[3] == 0.0f &&
      transform.values[7] == 0.0f &&
      transform.values[11] == 0.0f &&
      transform.values[15] == 1.0f;
}

bool Invert(const Transform& transform, Transform& inverse) {
  std::array<std::array<double, 8>, 4> augmented{};
  for (std::size_t row = 0; row < 4u; ++row) {
    for (std::size_t column = 0; column < 4u; ++column) {
      augmented[row][column] = transform.values[row * 4u + column];
    }
    augmented[row][row + 4u] = 1.0;
  }

  for (std::size_t column = 0; column < 4u; ++column) {
    std::size_t pivot_row = column;
    for (std::size_t row = column + 1u; row < 4u; ++row) {
      if (std::abs(augmented[row][column]) > std::abs(augmented[pivot_row][column])) {
        pivot_row = row;
      }
    }
    if (augmented[pivot_row][column] == 0.0) {
      return false;
    }
    if (pivot_row != column) {
      std::swap(augmented[pivot_row], augmented[column]);
    }

    const double pivot = augmented[column][column];
    for (double& value : augmented[column]) {
      value /= pivot;
    }
    for (std::size_t row = 0; row < 4u; ++row) {
      if (row == column) {
        continue;
      }
      const double factor = augmented[row][column];
      for (std::size_t entry = 0; entry < 8u; ++entry) {
        augmented[row][entry] -= factor * augmented[column][entry];
      }
    }
  }

  for (std::size_t row = 0; row < 4u; ++row) {
    for (std::size_t column = 0; column < 4u; ++column) {
      const double value = augmented[row][column + 4u];
      if (!std::isfinite(value) || value < -std::numeric_limits<float>::max() ||
          value > std::numeric_limits<float>::max()) {
        return false;
      }
      inverse.values[row * 4u + column] = static_cast<float>(value);
    }
  }
  return true;
}

std::vector<AlwaysIncludeVolume> ParseAlwaysIncludeVolumes(const Json& probes) {
  const Json& values = Required(probes, "always_include_volumes");
  if (!values.is_array()) {
    ConfigurationError("'always_include_volumes' must be an array");
  }

  std::vector<AlwaysIncludeVolume> volumes;
  volumes.reserve(values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (!values[index].is_object()) {
      ConfigurationError("always_include_volumes entries must be objects");
    }
    std::ostringstream description;
    description << "always_include_volumes[" << index << "].transform";
    const Transform local_to_world =
        ParseTransform(Required(values[index], "transform"), description.str());
    if (!IsAffine(local_to_world)) {
      ConfigurationError(description.str() + " must be affine");
    }
    Transform world_to_local{};
    if (!Invert(local_to_world, world_to_local)) {
      ConfigurationError(description.str() + " must be invertible");
    }
    volumes.push_back(AlwaysIncludeVolume{local_to_world, world_to_local});
  }
  return volumes;
}

ObjectDefinition ParseObject(const Json& object, const std::filesystem::path& config_directory) {
  if (!object.is_object()) {
    ConfigurationError("every world object must be an object");
  }
  const std::filesystem::path mesh_path(RequiredString(object, "mesh"));
  RequireContainedRelativePath(
      config_directory,
      mesh_path,
      "mesh paths must be relative",
      "mesh paths must not traverse reparse points");
  if (!std::filesystem::is_regular_file(config_directory / mesh_path)) {
    ConfigurationError("mesh file does not exist: " + mesh_path.string());
  }
  const Transform transform = ParseOptionalTransform(object);
  if (!IsAffine(transform)) {
    ConfigurationError("object transform must be affine");
  }
  return ObjectDefinition{
      ParseObjectId(Required(object, "object_id")),
      OptionalString(object, "label"),
      mesh_path.string(),
      transform,
  };
}
}  // namespace

BakeConfig LoadBakeConfig(const std::filesystem::path& config_path) {
  try {
    std::ifstream stream(config_path);
    if (!stream.is_open()) {
      ConfigurationError("cannot open file: " + config_path.string());
    }

    const auto config_directory = std::filesystem::weakly_canonical(
        std::filesystem::absolute(config_path).parent_path());
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
      objects.push_back(ParseObject(object, config_directory));
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
    const Json& dilation_radius = Required(voxel, "dilation_radius");
    if (!dilation_radius.is_number_unsigned() || dilation_radius.get<std::uint64_t>() == 0u ||
        dilation_radius.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
      ConfigurationError("dilation_radius must be a positive integer");
    }

    const Json& probes = Required(root, "probes");
    const float storage_cell_size = RequiredNumber(probes, "storage_cell_size");
    if (storage_cell_size != 4.0f) {
      ConfigurationError("storage_cell_size must equal 4");
    }
    const float delta = RequiredNumber(probes, "delta");
    if (delta < 0.0f || delta >= 2.0f) {
      ConfigurationError("'probes.delta' must be at least zero and less than 2");
    }
    auto always_include_volumes = ParseAlwaysIncludeVolumes(probes);

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
    RequireContainedRelativePath(
        config_directory,
        output_directory,
        "output directory must be relative",
        "output directory must not traverse reparse points");

    return BakeConfig{
        parsed_bounds,
        std::move(objects),
        VoxelSettings{voxel_size, dilation_radius.get<std::uint32_t>()},
        ProbeSettings{
            storage_cell_size,
            delta,
            std::move(always_include_volumes),
        },
        TraceSettings{face_resolution.get<std::uint32_t>(), max_distance},
        OutputSettings{output_directory, RequiredBool(output, "write_json"), RequiredBool(output, "write_binary")},
        config_directory,
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
