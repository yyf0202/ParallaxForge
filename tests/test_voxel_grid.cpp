#include "parallax_forge/voxel/voxel_grid.hpp"
#include "parallax_forge/voxel/volume_rasterizer.hpp"
#include "volume_rasterizer_limits.hpp"

#include <cassert>
#include <cstring>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using parallax_forge::gpu::GpuBuffer;
using parallax_forge::gpu::GpuContext;
using parallax_forge::voxel::GpuVoxelField;
using parallax_forge::voxel::VolumeRasterizer;
namespace raster_detail = parallax_forge::voxel::detail;
using parallax_forge::world::Bounds;
using parallax_forge::world::ImportedObject;
using parallax_forge::world::Transform;
using parallax_forge::world::Triangle;
using parallax_forge::world::Vec3;
using parallax_forge::world::VoxelSettings;
using parallax_forge::world::WorldModel;

bool IsUnavailableHardware(const std::runtime_error& error) {
  return std::string_view(error.what()).starts_with(
      "No hardware Direct3D 12 adapter with DXR tier 1.0 and "
      "Int64ShaderOps support");
}

std::vector<std::uint32_t> ReadField(GpuContext& context,
                                     const GpuVoxelField& field) {
  auto readback = GpuBuffer::Readback(context, field.final_field.byte_size());
  field.final_field.CopyToReadback(
      context, readback, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  std::vector<std::uint32_t> values(
      static_cast<std::size_t>(field.final_field.byte_size() /
                               sizeof(std::uint32_t)));
  void* mapped = nullptr;
  const D3D12_RANGE read_range{0, static_cast<SIZE_T>(
                                      field.final_field.byte_size())};
  if (FAILED(readback.resource()->Map(0, &read_range, &mapped))) {
    throw std::runtime_error("Failed to map voxel readback buffer.");
  }
  std::memcpy(values.data(), mapped,
              static_cast<std::size_t>(field.final_field.byte_size()));
  const D3D12_RANGE written_range{0, 0};
  readback.resource()->Unmap(0, &written_range);
  return values;
}

std::size_t Index(const parallax_forge::voxel::GridShape& grid,
                  std::uint32_t x, std::uint32_t y, std::uint32_t z) {
  return (static_cast<std::size_t>(z) * grid.y + y) * grid.x + x;
}

void VerifyInclusiveTransformedAabb(VolumeRasterizer& rasterizer,
                                    GpuContext& context) {
  auto translated = Transform::Identity();
  translated.values[12] = 1.0f;
  translated.values[13] = 1.0f;
  const WorldModel world{
      Bounds{{0.0f, 0.0f, 0.0f}, {4.0f, 4.0f, 1.0f}},
      std::vector<ImportedObject>{ImportedObject{
          1,
          0,
          translated,
          std::vector<Triangle>{Triangle{
              Vec3{0.1f, 0.1f, 0.1f},
              Vec3{1.1f, 0.1f, 0.1f},
              Vec3{0.1f, 1.1f, 0.1f}}}}}};

  const auto field = rasterizer.Rasterize(world, VoxelSettings{1.0f, 0});
  const auto values = ReadField(context, field);

  assert(values[Index(field.grid, 1, 1, 0)] == 1);
  assert(values[Index(field.grid, 2, 1, 0)] == 1);
  assert(values[Index(field.grid, 1, 2, 0)] == 1);
  assert(values[Index(field.grid, 2, 2, 0)] == 1);
  assert(values[Index(field.grid, 0, 0, 0)] == 0);
  assert(values[Index(field.grid, 3, 3, 0)] == 0);
}

void VerifySphericalDilation(VolumeRasterizer& rasterizer,
                             GpuContext& context) {
  const WorldModel world{
      Bounds{{0.0f, 0.0f, 0.0f}, {7.0f, 7.0f, 1.0f}},
      std::vector<ImportedObject>{ImportedObject{
          2,
          0,
          Transform::Identity(),
          std::vector<Triangle>{Triangle{
              Vec3{3.1f, 3.1f, 0.1f},
              Vec3{3.2f, 3.1f, 0.1f},
              Vec3{3.1f, 3.2f, 0.1f}}}}}};

  const auto field = rasterizer.Rasterize(world, VoxelSettings{1.0f, 2});
  const auto values = ReadField(context, field);

  assert(values[Index(field.grid, 3, 3, 0)] == 1);
  assert(values[Index(field.grid, 5, 3, 0)] == 1);
  assert(values[Index(field.grid, 4, 4, 0)] == 1);
  assert(values[Index(field.grid, 5, 4, 0)] == 0);
  assert(values[Index(field.grid, 0, 0, 0)] == 0);
}

void VerifyExtremeRadiusBounds(VolumeRasterizer& rasterizer,
                               GpuContext& context) {
  const WorldModel world{
      Bounds{{0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}},
      std::vector<ImportedObject>{ImportedObject{
          3,
          0,
          Transform::Identity(),
          std::vector<Triangle>{Triangle{
              Vec3{0.1f, 0.1f, 0.1f},
              Vec3{0.2f, 0.1f, 0.1f},
              Vec3{0.1f, 0.2f, 0.1f}}}}}};

  const auto field = rasterizer.Rasterize(
      world,
      VoxelSettings{
          1.0f,
          static_cast<std::uint32_t>(
              (std::numeric_limits<int>::max)())});
  const auto values = ReadField(context, field);

  assert(values[Index(field.grid, 0, 0, 0)] == 1);
  assert(values[Index(field.grid, 1, 0, 0)] == 1);
}

void VerifyBoundaryClamping(VolumeRasterizer& rasterizer,
                            GpuContext& context) {
  const WorldModel world{
      Bounds{{0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}},
      std::vector<ImportedObject>{ImportedObject{
          4,
          0,
          Transform::Identity(),
          std::vector<Triangle>{
              Triangle{
                  Vec3{-1.0f, 0.1f, 0.1f},
                  Vec3{-1.0f, 0.2f, 0.1f},
                  Vec3{-1.0f, 0.1f, 0.2f}},
              Triangle{
                  Vec3{2.0f, 0.1f, 0.1f},
                  Vec3{2.0f, 0.2f, 0.1f},
                  Vec3{2.0f, 0.1f, 0.2f}}}}}};

  const auto field = rasterizer.Rasterize(world, VoxelSettings{1.0f, 0});
  const auto values = ReadField(context, field);

  assert(values.size() == 2);
  assert(values[Index(field.grid, 0, 0, 0)] == 1);
  assert(values[Index(field.grid, 1, 0, 0)] == 1);
}

void VerifyHugePositiveBoundaryClamping(VolumeRasterizer& rasterizer,
                                        GpuContext& context) {
  const WorldModel world{
      Bounds{{0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}},
      std::vector<ImportedObject>{ImportedObject{
          5,
          0,
          Transform::Identity(),
          std::vector<Triangle>{Triangle{
              Vec3{1.0e20f, 0.1f, 0.1f},
              Vec3{1.0e20f, 0.2f, 0.1f},
              Vec3{1.0e20f, 0.1f, 0.2f}}}}}};

  const auto field = rasterizer.Rasterize(world, VoxelSettings{1.0f, 0});
  const auto values = ReadField(context, field);

  assert(values.size() == 2);
  assert(values[Index(field.grid, 0, 0, 0)] == 0);
  assert(values[Index(field.grid, 1, 0, 0)] == 1);
}

void VerifyHugeNegativeBoundaryClamping(VolumeRasterizer& rasterizer,
                                        GpuContext& context) {
  const WorldModel world{
      Bounds{{0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}},
      std::vector<ImportedObject>{ImportedObject{
          6,
          0,
          Transform::Identity(),
          std::vector<Triangle>{Triangle{
              Vec3{-1.0e20f, 0.1f, 0.1f},
              Vec3{-1.0e20f, 0.2f, 0.1f},
              Vec3{-1.0e20f, 0.1f, 0.2f}}}}}};

  const auto field = rasterizer.Rasterize(world, VoxelSettings{1.0f, 0});
  const auto values = ReadField(context, field);

  assert(values.size() == 2);
  assert(values[Index(field.grid, 0, 0, 0)] == 1);
  assert(values[Index(field.grid, 1, 0, 0)] == 0);
}

}  // namespace

void VerifyRetainedMath() {
  using parallax_forge::voxel::InDilationSphere;
  using parallax_forge::voxel::MakeGrid;

  const Bounds bounds{{0.0f, 0.0f, 0.0f}, {8.1f, 4.0f, 4.0f}};
  const auto grid = MakeGrid(bounds, 1.0f);

  assert(grid.x == 9);
  assert(grid.y == 4);
  assert(grid.z == 4);
  assert(grid.origin.x == bounds.min.x);
  assert(grid.origin.y == bounds.min.y);
  assert(grid.origin.z == bounds.min.z);
  assert(grid.voxel_size == 1.0f);
  assert(InDilationSphere(3, 4, 0, 5));
  assert(!InDilationSphere(4, 4, 0, 5));

  const auto overflow_grid = MakeGrid(
      Bounds{{0.0f, 0.0f, 0.0f},
             {90000.0f, 95672.0f, 2142359552.0f}},
      1.0f);
  assert(overflow_grid.x == 90000);
  assert(overflow_grid.y == 95672);
  assert(overflow_grid.z == 2142359552u);
  bool voxel_overflow_rejected = false;
  try {
    static_cast<void>(raster_detail::CheckedVoxelCount(overflow_grid));
  } catch (const std::length_error&) {
    voxel_overflow_rejected = true;
  }
  assert(voxel_overflow_rejected);

  const std::uint64_t maximum_triangle_records =
      raster_detail::MaximumAddressableTriangleRecords();
  assert(maximum_triangle_records == 42949672u);
  raster_detail::ValidateTriangleRecordCount(maximum_triangle_records);
  bool excess_triangles_rejected = false;
  try {
    raster_detail::ValidateTriangleRecordCount(
        maximum_triangle_records + 1);
  } catch (const std::length_error&) {
    excess_triangles_rejected = true;
  }
  assert(excess_triangles_rejected);

  const Triangle finite_triangle{
      Vec3{1.0e20f, 0.0f, 0.0f},
      Vec3{1.0e20f, 1.0f, 0.0f},
      Vec3{1.0e20f, 0.0f, 1.0f}};
  raster_detail::ValidateFiniteTransformedTriangle(
      finite_triangle, Transform::Identity());

  auto overflowing_transform = Transform::Identity();
  overflowing_transform.values[0] =
      (std::numeric_limits<float>::max)();
  bool non_finite_transformed_vertex_rejected = false;
  try {
    raster_detail::ValidateFiniteTransformedTriangle(
        Triangle{
            Vec3{2.0f, 0.0f, 0.0f},
            Vec3{2.0f, 1.0f, 0.0f},
            Vec3{2.0f, 0.0f, 1.0f}},
        overflowing_transform);
  } catch (const std::invalid_argument&) {
    non_finite_transformed_vertex_rejected = true;
  }
  assert(non_finite_transformed_vertex_rejected);
}

int VerifyGpuRasterizer() {
  try {
    auto context = GpuContext::Create();
    VolumeRasterizer rasterizer(context);
    VerifyInclusiveTransformedAabb(rasterizer, context);
    VerifySphericalDilation(rasterizer, context);
    VerifyExtremeRadiusBounds(rasterizer, context);
    VerifyBoundaryClamping(rasterizer, context);
    VerifyHugePositiveBoundaryClamping(rasterizer, context);
    VerifyHugeNegativeBoundaryClamping(rasterizer, context);
  } catch (const std::runtime_error& error) {
    if (IsUnavailableHardware(error)) {
      return 77;
    }
    throw;
  }
  return 0;
}

int main(int argc, char** argv) {
  VerifyRetainedMath();
  if (argc == 2 && std::string_view(argv[1]) == "--gpu") {
    return VerifyGpuRasterizer();
  }
  assert(argc == 1);
  return 0;
}
