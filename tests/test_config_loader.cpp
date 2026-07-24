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
  "voxel": { "size": 0.5, "clearance": 0.1 },
  "probes": { "spacing": 1.0 },
  "trace": { "face_resolution": 64, "max_distance": 100.0 },
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
  assert(config.voxel.size == 0.5f);
  assert(config.output.directory == std::filesystem::path("output"));
  assert(!config.output.directory.is_absolute());
  assert(config.objects[0].mesh_path == "meshes/floor.obj");

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
    "probes": { "spacing": 1.0 },
    "trace": { "face_resolution": 64, "max_distance": 100.0 },
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
    "voxel": { "size": 0.0, "clearance": 0.1 },
    "probes": { "spacing": 1.0 },
    "trace": { "face_resolution": 64, "max_distance": 100.0 },
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
    "voxel": { "size": 0.5, "clearance": 0.1 },
    "probes": { "spacing": 1.0 },
    "trace": { "face_resolution": 64, "max_distance": 100.0 },
    "output": { "directory": "output", "write_json": true, "write_binary": true }
  })");
  const auto placeholder = invalid_configuration.find("__ABSOLUTE_MESH__");
  invalid_configuration.replace(placeholder, std::string_view("__ABSOLUTE_MESH__").size(),
                                std::string("C") + ":/private/mesh.obj");
  WriteFile(invalid_path, invalid_configuration);

  AssertConfigurationError(invalid_path, "mesh paths must be relative");

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
