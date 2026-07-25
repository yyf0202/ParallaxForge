#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <parallax_forge/export/json_visibility_writer.hpp>
#include <parallax_forge/export/pfvis_codec.hpp>
#include <parallax_forge/export/visibility_output_pair.hpp>
#include <parallax_forge/gpu/gpu_context.hpp>
#include <parallax_forge/sampling/probe_generator.hpp>
#include <parallax_forge/tracing/visibility_bake_engine.hpp>
#include <parallax_forge/voxel/volume_rasterizer.hpp>
#include <parallax_forge/world/config_loader.hpp>
#include <parallax_forge/world/obj_world_importer.hpp>

namespace {

void PrintUsage() {
  std::cerr
      << "usage: parallax-forge-bake --config <bake.json> "
         "(--validate|--bake)\n";
}

parallax_forge::export_data::VisibilityCatalog RestoreConfiguredObjects(
    const parallax_forge::world::BakeConfig& config,
    const parallax_forge::export_data::VisibilityCatalog& traced_catalog) {
  parallax_forge::export_data::VisibilityCatalog catalog(config.objects);
  if (catalog.Objects().size() != traced_catalog.Objects().size()) {
    throw std::runtime_error(
        "traced object count does not match the bake configuration");
  }
  for (std::size_t index = 0; index < catalog.Objects().size(); ++index) {
    if (catalog.Objects()[index].object_id !=
        traced_catalog.Objects()[index].object_id) {
      throw std::runtime_error(
          "traced object IDs do not match configured public object IDs");
    }
  }
  for (const auto& probe : traced_catalog.Probes()) {
    catalog.AddProbe(probe);
  }
  return catalog;
}

void RunBake(const parallax_forge::world::BakeConfig& config) {
  auto context = parallax_forge::gpu::GpuContext::Create();
  if (!context.SupportsDxr()) {
    throw std::runtime_error(
        "DirectX Raytracing tier 1.0 support is required");
  }

  const auto world = parallax_forge::world::ImportWorld(config);
  parallax_forge::voxel::VolumeRasterizer rasterizer(context);
  const auto voxel_field = rasterizer.Rasterize(world, config.voxel);

  parallax_forge::sampling::ProbeGenerator probe_generator(context);
  const auto probes =
      probe_generator.Generate(voxel_field, config.bounds, config.probes);
  if (probes.empty()) {
    throw std::runtime_error("probe generation produced no probes");
  }

  parallax_forge::tracing::VisibilityBakeEngine visibility_baker(context);
  const auto traced_catalog =
      visibility_baker.Bake(world, probes, config.trace);
  const auto catalog = RestoreConfiguredObjects(config, traced_catalog);

  const auto output_directory =
      config.config_directory / config.output.directory;
  std::filesystem::create_directories(output_directory);
  if (config.output.write_json && config.output.write_binary) {
    parallax_forge::export_data::WriteVisibilityOutputPair(
        catalog, output_directory / "visibility.json",
        output_directory / "visibility.pfvis");
  } else if (config.output.write_json) {
    parallax_forge::export_data::WriteJsonVisibility(
        catalog, output_directory / "visibility.json");
  } else if (config.output.write_binary) {
    parallax_forge::export_data::WritePfvis(
        catalog, output_directory / "visibility.pfvis");
  }

  std::cout << "bake complete: " << catalog.Objects().size()
            << " objects, " << catalog.Probes().size() << " probes\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 || std::string_view(argv[1]) != "--config") {
    PrintUsage();
    return 1;
  }
  const std::string_view command(argv[3]);
  if (command != "--validate" && command != "--bake") {
    PrintUsage();
    return 1;
  }

  try {
    const auto config =
        parallax_forge::world::LoadBakeConfig(std::filesystem::path(argv[2]));
    if (command == "--validate") {
      std::cout << "configuration valid: " << config.objects.size()
                << " objects\n";
      return 0;
    }
    RunBake(config);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << (command == "--validate" ? "configuration error: "
                                          : "bake error: ")
              << error.what() << '\n';
    return 1;
  }
}
