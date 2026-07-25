#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#define NOMINMAX
#include <Windows.h>

#include <nlohmann/json.hpp>

#include <parallax_forge/world/config_loader.hpp>

#undef assert
#define assert(expression)                                                                          \
  do {                                                                                              \
    if (!(expression)) {                                                                            \
      std::fprintf(stderr, "assertion failed: %s (%s:%d)\n", #expression, __FILE__, __LINE__);     \
      std::_Exit(1);                                                                                 \
    }                                                                                               \
  } while (false)

namespace {
class TemporaryDirectory {
 public:
  TemporaryDirectory()
      : path_(std::filesystem::temp_directory_path() / "parallax_forge_config_loader_test") {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_ / "meshes");
  }

  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

  [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream file(path);
  assert(file.is_open());
  file << contents;
}

bool TryCreateDirectoryLink(
    const std::filesystem::path& link,
    const std::filesystem::path& target) {
  if (CreateSymbolicLinkW(
          link.c_str(), target.c_str(),
          SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0) {
    return true;
  }
  DWORD error = GetLastError();
  if (error == ERROR_INVALID_PARAMETER) {
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY) != 0) {
      return true;
    }
    error = GetLastError();
  }
  if (error == ERROR_PRIVILEGE_NOT_HELD) {
    return false;
  }
  throw std::runtime_error("failed to create test directory link");
}

void AssertConfigurationError(
    const std::filesystem::path& path, const std::string_view expected_message = {}) {
  bool rejected = false;
  try {
    static_cast<void>(parallax_forge::world::LoadBakeConfig(path));
  } catch (const std::runtime_error& error) {
    const std::string message(error.what());
    rejected = message.starts_with("configuration:") &&
        (expected_message.empty() || message.find(expected_message) != std::string::npos);
  }
  assert(rejected);
}

constexpr const char* kValidJson = R"({
  "world": {
    "bounds": { "min": [-8, 0, -8], "max": [8, 6, 8] },
    "objects": [
      { "object_id": 100, "label": "floor", "mesh": "meshes/floor.obj" },
      { "object_id": 200, "label": "central_block", "mesh": "meshes/block.obj" }
    ]
  },
  "voxel": { "size": 1.0, "dilation_radius": 5 },
  "probes": {
    "storage_cell_size": 4.0,
    "delta": 0.2,
    "always_include_volumes": []
  },
  "trace": { "face_resolution": 600, "max_distance": 5000.0 },
  "output": { "directory": "output", "write_json": true, "write_binary": true }
})";
}  // namespace

int RunTest() {
#ifdef _MSC_VER
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  using namespace parallax_forge::world;

  TemporaryDirectory temporary_directory;
  WriteFile(temporary_directory.Path() / "meshes/floor.obj", "floor");
  WriteFile(temporary_directory.Path() / "meshes/block.obj", "block");
  const auto valid_path = temporary_directory.Path() / "valid.json";
  WriteFile(valid_path, kValidJson);

  const BakeConfig config = LoadBakeConfig(valid_path);
  assert(config.objects.size() == 2u);
  assert(config.bounds.min.x == -8.0f);
  assert(config.voxel.size == 1.0f);
  assert(config.voxel.dilation_radius == 5u);
  assert(config.probes.storage_cell_size == 4.0f);
  assert(config.probes.delta == 0.2f);
  assert(config.probes.always_include_volumes.empty());
  assert(config.output.directory == std::filesystem::path("output"));
  assert(!config.output.directory.is_absolute());
  assert(config.objects[0].mesh_path == "meshes/floor.obj");

  auto empty_world_json = nlohmann::json::parse(kValidJson);
  empty_world_json["world"]["bounds"]["min"] = {0, 0, 0};
  empty_world_json["world"]["bounds"]["max"] = {8, 8, 8};
  empty_world_json["world"]["objects"] = nlohmann::json::array();
  empty_world_json["output"]["directory"] = "out";
  const auto empty_world_path = temporary_directory.Path() / "empty_world.json";
  WriteFile(empty_world_path, empty_world_json.dump());
  assert(LoadBakeConfig(empty_world_path).probes.storage_cell_size == 4.0f);

  auto optional_label_json = nlohmann::json::parse(kValidJson);
  optional_label_json["world"]["objects"][0].erase("label");
  const auto optional_label_path = temporary_directory.Path() / "optional_label.json";
  WriteFile(optional_label_path, optional_label_json.dump());
  const BakeConfig optional_label_config = LoadBakeConfig(optional_label_path);
  assert(optional_label_config.objects[0].label.empty());

  auto invalid_label_json = nlohmann::json::parse(kValidJson);
  invalid_label_json["world"]["objects"][0]["label"] = 42;
  const auto invalid_label_path = temporary_directory.Path() / "invalid_label.json";
  WriteFile(invalid_label_path, invalid_label_json.dump());
  AssertConfigurationError(invalid_label_path, "'label' must be a string");

  const auto missing_field_path = temporary_directory.Path() / "missing_field.json";
  WriteFile(missing_field_path, R"({
    "world": {
      "bounds": { "min": [-8, 0, -8], "max": [8, 6, 8] },
      "objects": [
        { "object_id": 100, "label": "floor", "mesh": "meshes/floor.obj" }
      ]
    },
    "probes": {
      "storage_cell_size": 4.0,
      "delta": 0.2,
      "always_include_volumes": []
    },
    "trace": { "face_resolution": 600, "max_distance": 5000.0 },
    "output": { "directory": "output", "write_json": true, "write_binary": true }
  })");
  AssertConfigurationError(missing_field_path);

  const auto nonpositive_setting_path = temporary_directory.Path() / "nonpositive_setting.json";
  WriteFile(nonpositive_setting_path, R"({
    "world": {
      "bounds": { "min": [-8, 0, -8], "max": [8, 6, 8] },
      "objects": [
        { "object_id": 100, "label": "floor", "mesh": "meshes/floor.obj" }
      ]
    },
    "voxel": { "size": 0.0, "dilation_radius": 5 },
    "probes": {
      "storage_cell_size": 4.0,
      "delta": 0.2,
      "always_include_volumes": []
    },
    "trace": { "face_resolution": 600, "max_distance": 5000.0 },
    "output": { "directory": "output", "write_json": true, "write_binary": true }
  })");
  AssertConfigurationError(nonpositive_setting_path);

  const auto invalid_path = temporary_directory.Path() / "absolute_mesh.json";
  auto invalid_configuration = std::string(R"({
    "world": {
      "bounds": { "min": [-8, 0, -8], "max": [8, 6, 8] },
      "objects": [
        { "object_id": 100, "label": "floor", "mesh": "__ABSOLUTE_MESH__" }
      ]
    },
    "voxel": { "size": 1.0, "dilation_radius": 5 },
    "probes": {
      "storage_cell_size": 4.0,
      "delta": 0.2,
      "always_include_volumes": []
    },
    "trace": { "face_resolution": 600, "max_distance": 5000.0 },
    "output": { "directory": "output", "write_json": true, "write_binary": true }
  })");
  const auto placeholder = invalid_configuration.find("__ABSOLUTE_MESH__");
  invalid_configuration.replace(placeholder, std::string_view("__ABSOLUTE_MESH__").size(),
                                std::string("C") + ":/private/mesh.obj");
  WriteFile(invalid_path, invalid_configuration);

  AssertConfigurationError(invalid_path, "mesh paths must be relative");

  auto invalid_storage_cell_json = nlohmann::json::parse(kValidJson);
  invalid_storage_cell_json["probes"]["storage_cell_size"] = 8.0;
  const auto invalid_storage_cell_path =
      temporary_directory.Path() / "invalid_storage_cell_size.json";
  WriteFile(invalid_storage_cell_path, invalid_storage_cell_json.dump());
  AssertConfigurationError(invalid_storage_cell_path, "storage_cell_size must equal 4");

  auto invalid_dilation_radius_json = nlohmann::json::parse(kValidJson);
  invalid_dilation_radius_json["voxel"]["dilation_radius"] = 0;
  const auto invalid_dilation_radius_path =
      temporary_directory.Path() / "invalid_dilation_radius.json";
  WriteFile(invalid_dilation_radius_path, invalid_dilation_radius_json.dump());
  AssertConfigurationError(
      invalid_dilation_radius_path, "dilation_radius must be a positive integer");

  auto singular_volume_json = nlohmann::json::parse(kValidJson);
  singular_volume_json["probes"]["always_include_volumes"] = nlohmann::json::array({
      {{"transform",
        {1.0, 0.0, 0.0, 0.0,
         0.0, 0.0, 0.0, 0.0,
         0.0, 0.0, 1.0, 0.0,
         0.0, 0.0, 0.0, 1.0}}}});
  const auto singular_volume_path = temporary_directory.Path() / "singular_volume.json";
  WriteFile(singular_volume_path, singular_volume_json.dump());
  AssertConfigurationError(
      singular_volume_path, "always_include_volumes[0].transform must be invertible");

  auto perspective_volume_json = nlohmann::json::parse(kValidJson);
  perspective_volume_json["probes"]["always_include_volumes"] =
      nlohmann::json::array({
          {{"transform",
            {1.0, 0.0, 0.0, 0.25,
             0.0, 1.0, 0.0, 0.0,
             0.0, 0.0, 1.0, 0.0,
             0.0, 0.0, 0.0, 1.0}}}});
  const auto perspective_volume_path =
      temporary_directory.Path() / "perspective_volume.json";
  WriteFile(perspective_volume_path, perspective_volume_json.dump());
  AssertConfigurationError(
      perspective_volume_path,
      "always_include_volumes[0].transform must be affine");

  auto corrupt_homogeneous_volume_json = nlohmann::json::parse(kValidJson);
  corrupt_homogeneous_volume_json["probes"]["always_include_volumes"] =
      nlohmann::json::array({
          {{"transform",
            {1.0, 0.0, 0.0, 0.0,
             0.0, 1.0, 0.0, 0.0,
             0.0, 0.0, 1.0, 0.0,
             0.0, 0.0, 0.0, 2.0}}}});
  const auto corrupt_homogeneous_volume_path =
      temporary_directory.Path() / "corrupt_homogeneous_volume.json";
  WriteFile(
      corrupt_homogeneous_volume_path,
      corrupt_homogeneous_volume_json.dump());
  AssertConfigurationError(
      corrupt_homogeneous_volume_path,
      "always_include_volumes[0].transform must be affine");

  auto perspective_object_json = nlohmann::json::parse(kValidJson);
  perspective_object_json["world"]["objects"][0]["transform"] = {
      1.0, 0.0, 0.0, 0.25,
      0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      0.0, 0.0, 0.0, 1.0};
  const auto perspective_object_path =
      temporary_directory.Path() / "perspective_object.json";
  WriteFile(perspective_object_path, perspective_object_json.dump());
  AssertConfigurationError(perspective_object_path, "object transform must be affine");

  auto corrupt_homogeneous_object_json = nlohmann::json::parse(kValidJson);
  corrupt_homogeneous_object_json["world"]["objects"][0]["transform"] = {
      1.0, 0.0, 0.0, 0.0,
      0.0, 1.0, 0.0, 0.0,
      0.0, 0.0, 1.0, 0.0,
      0.0, 0.0, 0.0, 2.0};
  const auto corrupt_homogeneous_object_path =
      temporary_directory.Path() / "corrupt_homogeneous_object.json";
  WriteFile(corrupt_homogeneous_object_path, corrupt_homogeneous_object_json.dump());
  AssertConfigurationError(
      corrupt_homogeneous_object_path, "object transform must be affine");

  auto affine_volume_json = nlohmann::json::parse(kValidJson);
  affine_volume_json["probes"]["always_include_volumes"] =
      nlohmann::json::array({
          {{"transform",
            {0.0, 2.0, 0.0, 0.0,
             -3.0, 0.0, 0.0, 0.0,
             0.0, 0.0, 4.0, 0.0,
             5.0, 6.0, 7.0, 1.0}}}});
  const auto affine_volume_path =
      temporary_directory.Path() / "affine_volume.json";
  WriteFile(affine_volume_path, affine_volume_json.dump());
  const auto affine_config = LoadBakeConfig(affine_volume_path);
  assert(affine_config.probes.always_include_volumes.size() == 1u);
  assert(affine_config.probes.always_include_volumes[0]
             .local_to_world.values[1] == 2.0f);
  assert(affine_config.probes.always_include_volumes[0]
             .local_to_world.values[4] == -3.0f);
  assert(affine_config.probes.always_include_volumes[0]
             .world_to_local.values[15] == 1.0f);

  for (const auto& [name, mesh_path] : std::vector<std::pair<std::string, std::string>>{
           {"drive_relative_mesh", R"(C:meshes\floor.obj)"},
           {"root_relative_mesh", R"(\meshes\floor.obj)"},
           {"drive_rooted_mesh", std::string("C") + R"(:\meshes\floor.obj)"},
           {"escaping_mesh", R"(meshes\..\..\outside.obj)"},
       }) {
    auto invalid_mesh_json = nlohmann::json::parse(kValidJson);
    invalid_mesh_json["world"]["objects"][0]["mesh"] = mesh_path;
    const auto path = temporary_directory.Path() / (name + ".json");
    WriteFile(path, invalid_mesh_json.dump());
    AssertConfigurationError(path, "mesh paths must be relative");
  }

  for (const auto& [name, output_path] : std::vector<std::pair<std::string, std::string>>{
           {"drive_relative_output", R"(C:output)"},
           {"root_relative_output", R"(\output)"},
           {"drive_rooted_output", std::string("C") + R"(:\output)"},
           {"escaping_output", R"(cache\..\..\output)"},
       }) {
    auto invalid_output_json = nlohmann::json::parse(kValidJson);
    invalid_output_json["output"]["directory"] = output_path;
    const auto path = temporary_directory.Path() / (name + ".json");
    WriteFile(path, invalid_output_json.dump());
    AssertConfigurationError(path, "output directory must be relative");
  }

  const auto outside_directory =
      temporary_directory.Path().parent_path() / "parallax-forge-config-loader-outside";
  std::filesystem::remove_all(outside_directory);
  std::filesystem::create_directories(outside_directory);
  WriteFile(outside_directory / "floor.obj", "outside");

  if (TryCreateDirectoryLink(
          temporary_directory.Path() / "linked-meshes", outside_directory)) {
    auto linked_mesh_json = nlohmann::json::parse(kValidJson);
    linked_mesh_json["world"]["objects"][0]["mesh"] = "linked-meshes/floor.obj";
    const auto linked_mesh_path = temporary_directory.Path() / "linked_mesh.json";
    WriteFile(linked_mesh_path, linked_mesh_json.dump());
    AssertConfigurationError(linked_mesh_path, "mesh paths must not traverse reparse points");
  } else {
    std::printf("skipping mesh reparse test: link creation is not permitted\n");
  }

  if (TryCreateDirectoryLink(
          temporary_directory.Path() / "linked-output", outside_directory)) {
    auto linked_output_json = nlohmann::json::parse(kValidJson);
    linked_output_json["output"]["directory"] = "linked-output";
    const auto linked_output_path = temporary_directory.Path() / "linked_output.json";
    WriteFile(linked_output_path, linked_output_json.dump());
    AssertConfigurationError(linked_output_path, "output directory must not traverse reparse points");
  } else {
    std::printf("skipping output reparse test: link creation is not permitted\n");
  }
  std::filesystem::remove_all(outside_directory);
  return 0;
}

int main() {
  try {
    return RunTest();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "unexpected exception: %s\n", error.what());
    return 1;
  }
}
