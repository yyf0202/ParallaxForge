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
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#define NOMINMAX
#include <Windows.h>

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
  file.put(1);
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
  assert(decoded.Probes().at(0).visible_object_ids == std::vector<world::ObjectId>({100u, 200u}));

  const std::vector<std::uint8_t> golden_bytes{
      'P', 'F', 'V', '1',
      0x01, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00,
      0x64, 0x00, 0x00, 0x00,
      0xc8, 0x00, 0x00, 0x00,
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
  OverwriteByte(path, 4, 2);
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
  OverwriteU32(path, 40, 3u);
  assert(IsRejected(path));

  export_data::WritePfvis(source, path);
  OverwriteU32(path, 48, 999u);
  assert(IsRejected(path));

  for (const auto [coordinate_offset, invalid_coordinate_bits] :
       {std::pair<std::streamoff, std::uint32_t>{28, 0x7fc00000u},
        std::pair<std::streamoff, std::uint32_t>{32, 0x7f800000u},
        std::pair<std::streamoff, std::uint32_t>{36, 0xff800000u}}) {
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

int main() {
  try {
    return RunTest();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "unexpected exception: %s\n", error.what());
    return 1;
  }
}
