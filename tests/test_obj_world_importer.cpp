#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#include <parallax_forge/world/config_loader.hpp>
#include <parallax_forge/world/obj_world_importer.hpp>

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
      : path_(std::filesystem::temp_directory_path() / "parallax_forge_obj_importer_test") {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_ / "meshes");
  }

  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

  [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, const std::string_view contents) {
  std::ofstream file(path);
  assert(file.is_open());
  file << contents;
}

bool ThrowsImport(
    const parallax_forge::world::BakeConfig& config, const std::string_view expected_message) {
  try {
    static_cast<void>(parallax_forge::world::ImportWorld(config));
  } catch (const std::runtime_error& error) {
    const std::string message(error.what());
    return message.starts_with("world import:") &&
        message.find(expected_message) != std::string::npos;
  }
  return false;
}

constexpr std::string_view kConfiguration = R"({
  "world": {
    "bounds": { "min": [0, 0, 0], "max": [8, 8, 8] },
    "objects": [
      { "object_id": 200, "mesh": "meshes/triangle.obj" },
      { "object_id": 100, "mesh": "meshes/quad.obj" }
    ]
  },
  "voxel": { "size": 1.0, "dilation_radius": 5 },
  "probes": {
    "storage_cell_size": 4.0,
    "delta": 0.2,
    "always_include_volumes": []
  },
  "trace": { "face_resolution": 600, "max_distance": 5000.0 },
  "output": { "directory": "out", "write_json": true, "write_binary": true }
})";
}  // namespace

int RunTest() {
#ifdef _MSC_VER
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  using namespace parallax_forge::world;

  TemporaryDirectory temporary_directory;
  const auto quad_path = temporary_directory.Path() / "meshes/quad.obj";
  WriteFile(quad_path, R"(
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
f 1 2 3 4
)");
  WriteFile(temporary_directory.Path() / "meshes/triangle.obj", R"(
v 0 0 0
v 0 1 0
v 0 0 1
f 1 2 3
)");
  const auto config_path = temporary_directory.Path() / "bake.json";
  WriteFile(config_path, kConfiguration);
  const BakeConfig config = LoadBakeConfig(config_path);

  const auto world = ImportWorld(config);
  assert(world.objects.size() == 2u);
  assert(world.objects[0].object_id == 100u);
  assert(world.objects[0].slot == 0u);
  assert(world.objects[0].triangles.size() == 2u);
  assert(world.objects[1].object_id == 200u);
  assert(world.objects[1].slot == 1u);
  assert(world.objects[1].triangles.size() == 1u);

  assert(std::filesystem::remove(quad_path));
  assert(ThrowsImport(config, "could not be loaded"));

  WriteFile(quad_path, R"(
v 0 0 0
v 1 0 0
v 1 1 0
)");
  assert(ThrowsImport(config, "yielded no triangles"));
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
