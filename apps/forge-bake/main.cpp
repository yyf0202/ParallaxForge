#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <parallax_forge/world/config_loader.hpp>

int main(int argc, char** argv) {
  if (argc != 4 || std::string_view(argv[1]) != "--config" || std::string_view(argv[3]) != "--validate") {
    std::cerr << "usage: parallax-forge-bake --config <bake.json> --validate\n";
    return 1;
  }

  try {
    const auto config = parallax_forge::world::LoadBakeConfig(std::filesystem::path(argv[2]));
    std::cout << "configuration valid: " << config.objects.size() << " objects\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "configuration error: " << error.what() << '\n';
    return 1;
  }
}
