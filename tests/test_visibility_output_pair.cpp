#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

#define NOMINMAX
#include <Windows.h>

#include <parallax_forge/export/visibility_output_pair.hpp>

#undef assert
#define assert(expression)                                                                      \
  do {                                                                                          \
    if (!(expression)) {                                                                        \
      std::fprintf(stderr, "assertion failed: %s (%s:%d)\n", #expression, __FILE__, __LINE__); \
      std::_Exit(1);                                                                             \
    }                                                                                           \
  } while (false)

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory()
      : path_(std::filesystem::temp_directory_path() /
              "parallax_forge_visibility_output_pair_test") {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() { std::filesystem::remove_all(path_); }

  [[nodiscard]] const std::filesystem::path& Path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

class ExclusiveFileLock {
 public:
  explicit ExclusiveFileLock(const std::filesystem::path& path)
      : handle_(CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
    assert(handle_ != INVALID_HANDLE_VALUE);
  }

  ~ExclusiveFileLock() { CloseHandle(handle_); }

 private:
  HANDLE handle_;
};

std::uint64_t HashFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  assert(file.is_open());
  std::uint64_t hash = 14695981039346656037ull;
  for (char value : std::string(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>())) {
    hash ^= static_cast<unsigned char>(value);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::vector<std::string> DirectoryEntries(
    const std::filesystem::path& directory) {
  std::vector<std::string> entries;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    entries.push_back(entry.path().filename().string());
  }
  std::sort(entries.begin(), entries.end());
  return entries;
}

}  // namespace

int RunTest() {
#ifdef _MSC_VER
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  using namespace parallax_forge;

  TemporaryDirectory temporary_directory;
  const auto json_path = temporary_directory.Path() / "visibility.json";
  const auto pfvis_path = temporary_directory.Path() / "visibility.pfvis";

  export_data::VisibilityCatalog old_catalog({
      world::ObjectDefinition{
          300u, "old", {}, world::Transform::Identity()},
  });
  old_catalog.AddProbe({1u, {1.0f, 2.0f, 3.0f}, {300u}});
  export_data::WriteVisibilityOutputPair(
      old_catalog, json_path, pfvis_path);
  const std::uint64_t old_json_hash = HashFile(json_path);
  const std::uint64_t old_pfvis_hash = HashFile(pfvis_path);

  export_data::VisibilityCatalog new_catalog({
      world::ObjectDefinition{
          100u, "floor", {}, world::Transform::Identity()},
      world::ObjectDefinition{
          200u, "block", {}, world::Transform::Identity()},
  });
  new_catalog.AddProbe({2u, {4.0f, 5.0f, 6.0f}, {100u, 200u}});

  {
    ExclusiveFileLock lock(pfvis_path);
    bool replacement_rejected = false;
    try {
      export_data::WriteVisibilityOutputPair(
          new_catalog, json_path, pfvis_path);
    } catch (const std::filesystem::filesystem_error& error) {
      replacement_rejected =
          std::string(error.what()).find("visibility output pair") !=
          std::string::npos;
    }
    assert(replacement_rejected);
  }

  assert(HashFile(json_path) == old_json_hash);
  assert(HashFile(pfvis_path) == old_pfvis_hash);
  assert(DirectoryEntries(temporary_directory.Path()) ==
         std::vector<std::string>({"visibility.json", "visibility.pfvis"}));
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
