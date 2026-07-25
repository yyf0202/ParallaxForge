#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
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

#include <parallax_forge/export/pfvis_codec.hpp>

#undef assert
#define assert(expression)                                                                          \
  do {                                                                                              \
    if (!(expression)) {                                                                            \
      std::fprintf(stderr, "assertion failed: %s (%s:%d)\n", #expression, __FILE__, __LINE__);     \
      std::_Exit(1);                                                                                 \
    }                                                                                               \
  } while (false)

namespace {

bool IsRejected(const std::filesystem::path& path) {
  try {
    static_cast<void>(parallax_forge::export_data::ReadPfvis(path));
  } catch (const std::runtime_error&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

void OverwriteByte(const std::filesystem::path& path, std::streamoff offset, std::uint8_t value) {
  std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
  file.seekp(offset);
  file.put(static_cast<char>(value));
}

void OverwriteU32(const std::filesystem::path& path, std::streamoff offset, std::uint32_t value) {
  for (std::streamoff index = 0; index < 4; ++index) {
    OverwriteByte(path, offset + index, static_cast<std::uint8_t>(value >> (index * 8)));
  }
}

void WriteHeader(
    const std::filesystem::path& path, std::uint32_t object_count, std::uint32_t probe_count) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write("PFV1", 4);
  file.put(2);
  file.put(0);
  file.put(0);
  file.put(0);
  for (const auto value : {object_count, probe_count}) {
    for (std::uint32_t shift = 0; shift < 32; shift += 8) {
      file.put(static_cast<char>(value >> shift));
    }
  }
}

std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
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

void RequireComparison(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

int CompareVisibilityOutputs(
    const std::filesystem::path& json_path,
    const std::filesystem::path& pfvis_path) {
  std::ifstream json_file(json_path);
  if (!json_file) {
    throw std::runtime_error("failed to open visibility JSON");
  }
  const auto json = nlohmann::json::parse(json_file);
  const auto pfvis = parallax_forge::export_data::ReadPfvis(pfvis_path);

  RequireComparison(
      json.at("format") == "parallax-forge.visibility",
      "visibility JSON format does not match");
  RequireComparison(
      json.at("version") == 1,
      "visibility JSON version does not match");

  const auto& json_objects = json.at("objects");
  RequireComparison(
      json_objects.is_array() &&
          json_objects.size() == pfvis.Objects().size(),
      "JSON/PFVIS object counts do not match");
  for (std::size_t index = 0; index < pfvis.Objects().size(); ++index) {
    const auto& json_object = json_objects.at(index);
    RequireComparison(
        json_object.at("object_id").get<parallax_forge::world::ObjectId>() ==
            pfvis.Objects()[index].object_id,
        "JSON/PFVIS ordered object IDs do not match");
    RequireComparison(
        json_object.at("label").is_string(),
        "visibility JSON object label is not a string");
    RequireComparison(
        json_object.at("label").get<std::string>() == pfvis.Objects()[index].label,
        "JSON/PFVIS ordered object labels do not match");
  }

  const auto& json_probes = json.at("probes");
  RequireComparison(
      json_probes.is_array() &&
          json_probes.size() == pfvis.Probes().size(),
      "JSON/PFVIS probe counts do not match");
  std::size_t visible_id_count = 0;
  for (std::size_t index = 0; index < pfvis.Probes().size(); ++index) {
    const auto& json_probe = json_probes.at(index);
    const auto& pfvis_probe = pfvis.Probes()[index];
    RequireComparison(
        json_probe.at("probe_id").get<
            parallax_forge::export_data::ProbeId>() ==
            pfvis_probe.probe_id,
        "JSON/PFVIS ordered probe IDs do not match");

    const auto& position = json_probe.at("position");
    RequireComparison(
        position.is_array() && position.size() == 3u,
        "visibility JSON probe position is invalid");
    RequireComparison(
        position.at(0).get<float>() == pfvis_probe.position.x &&
            position.at(1).get<float>() == pfvis_probe.position.y &&
            position.at(2).get<float>() == pfvis_probe.position.z,
        "JSON/PFVIS ordered probe positions do not match");

    RequireComparison(
        json_probe.at("visible_object_ids")
                .get<std::vector<parallax_forge::world::ObjectId>>() ==
            pfvis_probe.visible_object_ids,
        "JSON/PFVIS ordered visible object IDs do not match");
    visible_id_count += pfvis_probe.visible_object_ids.size();
  }

  std::printf(
      "decoded outputs match: %zu objects, %zu probes, %zu visible object IDs\n",
      pfvis.Objects().size(), pfvis.Probes().size(), visible_id_count);
  return 0;
}

}  // namespace

int RunTest() {
#ifdef _MSC_VER
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  using namespace parallax_forge;
  export_data::VisibilityCatalog source({
      world::ObjectDefinition{100u, "floor", "meshes/floor.obj", world::Transform::Identity()},
      world::ObjectDefinition{200u, "central_block", "meshes/block.obj", world::Transform::Identity()},
  });
  source.AddProbe({0u, {0.0f, 1.0f, 0.0f}, {100u, 200u}});
  const auto path = std::filesystem::temp_directory_path() / "parallax-forge-codec-test.pfvis";
  export_data::WritePfvis(source, path);
  const auto decoded = export_data::ReadPfvis(path);
  assert(decoded.Objects().size() == 2u);
  assert(decoded.Objects().at(0).label == "floor");
  assert(decoded.Objects().at(1).label == "central_block");
  assert(decoded.Probes().at(0).visible_object_ids == std::vector<world::ObjectId>({100u, 200u}));

  const std::vector<std::uint8_t> golden_bytes{
       'P', 'F', 'V', '1',
       0x02, 0x00, 0x00, 0x00,
       0x02, 0x00, 0x00, 0x00,
       0x01, 0x00, 0x00, 0x00,
       0x64, 0x00, 0x00, 0x00,
       0x05, 0x00, 0x00, 0x00,
       'f', 'l', 'o', 'o', 'r',
       0xc8, 0x00, 0x00, 0x00,
       0x0d, 0x00, 0x00, 0x00,
       'c', 'e', 'n', 't', 'r', 'a', 'l', '_', 'b', 'l', 'o', 'c', 'k',
       0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x80, 0x3f,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00,
      0x64, 0x00, 0x00, 0x00,
      0xc8, 0x00, 0x00, 0x00,
  };
  assert(ReadBytes(path) == golden_bytes);

  OverwriteByte(path, 0, 'X');
  assert(IsRejected(path));

  export_data::WritePfvis(source, path);
  OverwriteByte(path, 4, 3);
  assert(IsRejected(path));

  export_data::WritePfvis(source, path);
  OverwriteByte(path, 6, 1);
  assert(IsRejected(path));

  export_data::WritePfvis(source, path);
  std::filesystem::resize_file(path, 15);
  assert(IsRejected(path));

  export_data::WritePfvis(source, path);
  {
    std::ofstream file(path, std::ios::binary | std::ios::app);
    file.put(0);
  }
  assert(IsRejected(path));

  export_data::WritePfvis(source, path);
  OverwriteU32(path, 66, 3u);
  assert(IsRejected(path));

  export_data::WritePfvis(source, path);
  OverwriteU32(path, 74, 999u);
  assert(IsRejected(path));

  for (const auto [coordinate_offset, invalid_coordinate_bits] :
       {std::pair<std::streamoff, std::uint32_t>{54, 0x7fc00000u},
         std::pair<std::streamoff, std::uint32_t>{58, 0x7f800000u},
         std::pair<std::streamoff, std::uint32_t>{62, 0xff800000u}}) {
    export_data::WritePfvis(source, path);
    OverwriteU32(path, coordinate_offset, invalid_coordinate_bits);
    assert(IsRejected(path));
  }

  WriteHeader(path, std::numeric_limits<std::uint32_t>::max(), 0u);
  assert(IsRejected(path));

  WriteHeader(path, 0u, std::numeric_limits<std::uint32_t>::max());
  assert(IsRejected(path));

  export_data::VisibilityCatalog old_source({
      world::ObjectDefinition{300u, "old", {}, world::Transform::Identity()},
  });
  old_source.AddProbe({1u, {1.0f, 2.0f, 3.0f}, {300u}});
  export_data::WritePfvis(old_source, path);
  const auto old_bytes = ReadBytes(path);
  {
    ExclusiveFileLock lock(path);
    bool replacement_rejected = false;
    try {
      export_data::WritePfvis(source, path);
    } catch (const std::filesystem::filesystem_error& error) {
      replacement_rejected = std::string(error.what()).find("replace") != std::string::npos;
    }
    assert(replacement_rejected);
  }
  assert(ReadBytes(path) == old_bytes);
  auto temporary_path = path;
  temporary_path += ".tmp";
  assert(!std::filesystem::exists(temporary_path));

  std::filesystem::remove(path);
  return 0;
}

int main(int argc, char** argv) {
  try {
    if (argc == 4 && std::string_view(argv[1]) == "--compare-visibility") {
      return CompareVisibilityOutputs(argv[2], argv[3]);
    }
    if (argc != 1) {
      std::fprintf(
          stderr,
          "usage: parallax_forge_test_pfvis_codec "
          "[--compare-visibility <visibility.json> <visibility.pfvis>]\n");
      return 1;
    }
    return RunTest();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "unexpected exception: %s\n", error.what());
    return 1;
  }
}
