#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

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
parallax_forge::world::BakeConfig LoadDemoConfig() {
  const auto config_path =
      std::filesystem::path(PARALLAX_FORGE_SOURCE_DIR) / "assets/demo-chamber/bake.json";
  return parallax_forge::world::LoadBakeConfig(config_path);
}
}  // namespace

int RunTest() {
#ifdef _MSC_VER
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
  using namespace parallax_forge::world;

  const auto world = ImportWorld(LoadDemoConfig());
  assert(world.objects.size() == 2u);
  assert(world.objects[0].object_id == 100u);
  assert(world.objects[0].slot == 0u);
  assert(!world.objects[0].triangles.empty());
  assert(world.objects[1].object_id == 200u);
  assert(world.objects[1].slot == 1u);
  assert(!world.objects[1].triangles.empty());
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
