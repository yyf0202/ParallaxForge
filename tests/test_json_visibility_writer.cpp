#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#define NOMINMAX
#include <Windows.h>

#include <nlohmann/json.hpp>

#include <parallax_forge/export/json_visibility_writer.hpp>

#undef assert
#define assert(expression)                                                                          \
  do {                                                                                              \
    if (!(expression)) {                                                                            \
      std::fprintf(stderr, "assertion failed: %s (%s:%d)\n", #expression, __FILE__, __LINE__);     \
      std::_Exit(1);                                                                                 \
    }                                                                                               \
  } while (false)

namespace {

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::set<std::string> Keys(const nlohmann::json& object) {
  std::set<std::string> keys;
  for (const auto& [key, value] : object.items()) {
    static_cast<void>(value);
    keys.insert(key);
  }
  return keys;
}

class ExclusiveFileLock {
 public:
  explicit ExclusiveFileLock(const std::filesystem::path& path)
      : handle_(CreateFileW(
            path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
    assert(handle_ != INVALID_HANDLE_VALUE);
  }

  ~ExclusiveFileLock() { CloseHandle(handle_); }

 private:
  HANDLE handle_;
};

}  // namespace

int RunTest() {
#ifdef _MSC_VER
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  using namespace parallax_forge;

  export_data::VisibilityCatalog catalog({
      world::ObjectDefinition{200u, "central_block", "meshes/block.obj", world::Transform::Identity()},
      world::ObjectDefinition{100u, "floor", "meshes/floor.obj", world::Transform::Identity()},
  });
  catalog.AddProbe({9u, {2.0f, 3.0f, 4.0f}, {200u}});
  catalog.AddProbe({7u, {0.0f, 1.0f, 0.0f}, {200u, 100u, 200u}});

  const auto output = std::filesystem::temp_directory_path() / "parallax-forge-json-test.json";
  export_data::WriteJsonVisibility(catalog, output);
  {
    std::ifstream file(output);
    const auto json = nlohmann::json::parse(file);
    assert(Keys(json) == std::set<std::string>({"format", "objects", "probes", "version"}));
    assert(json.at("format") == "parallax-forge.visibility");
    assert(json.at("version").is_number_integer());
    assert(json.at("version") == 1);
    assert(json.at("objects").size() == 2u);
    assert(Keys(json.at("objects").at(0)) == std::set<std::string>({"label", "object_id"}));
    assert(json.at("objects").at(0).at("object_id") == 100u);
    assert(json.at("objects").at(0).at("label") == "floor");
    assert(json.at("objects").at(1).at("object_id") == 200u);
    assert(Keys(json.at("probes").at(0)) ==
           std::set<std::string>({"position", "probe_id", "visible_object_ids"}));
    assert(json.at("probes").at(0).at("probe_id") == 7u);
    assert(json.at("probes").at(0).at("position") == nlohmann::json::array({0.0f, 1.0f, 0.0f}));
    assert(json.at("probes").at(0).at("visible_object_ids") == nlohmann::json::array({100u, 200u}));
    assert(json.at("probes").at(1).at("probe_id") == 9u);
  }

  bool duplicate_probe_rejected = false;
  try {
    catalog.AddProbe({7u, {1.0f, 0.0f, 0.0f}, {100u}});
  } catch (const std::invalid_argument&) {
    duplicate_probe_rejected = true;
  }
  assert(duplicate_probe_rejected);

  bool unknown_object_rejected = false;
  try {
    catalog.AddProbe({8u, {1.0f, 0.0f, 0.0f}, {999u}});
  } catch (const std::invalid_argument&) {
    unknown_object_rejected = true;
  }
  assert(unknown_object_rejected);

  for (const auto invalid_position :
       {world::Vec3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
        world::Vec3{0.0f, std::numeric_limits<float>::infinity(), 0.0f},
        world::Vec3{0.0f, 0.0f, -std::numeric_limits<float>::infinity()}}) {
    bool non_finite_probe_rejected = false;
    try {
      catalog.AddProbe({10u, invalid_position, {100u}});
    } catch (const std::invalid_argument&) {
      non_finite_probe_rejected = true;
    }
    assert(non_finite_probe_rejected);
  }

  export_data::VisibilityCatalog old_catalog({
      world::ObjectDefinition{300u, "old", {}, world::Transform::Identity()},
  });
  old_catalog.AddProbe({1u, {1.0f, 2.0f, 3.0f}, {300u}});
  export_data::WriteJsonVisibility(old_catalog, output);
  const std::string old_contents = ReadText(output);
  {
    ExclusiveFileLock lock(output);
    bool replacement_rejected = false;
    try {
      export_data::WriteJsonVisibility(catalog, output);
    } catch (const std::filesystem::filesystem_error& error) {
      replacement_rejected = std::string(error.what()).find("replace") != std::string::npos;
    }
    assert(replacement_rejected);
  }
  assert(ReadText(output) == old_contents);
  auto temporary_output = output;
  temporary_output += ".tmp";
  assert(!std::filesystem::exists(temporary_output));

  std::filesystem::remove(output);
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
