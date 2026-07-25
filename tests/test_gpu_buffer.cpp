#include "parallax_forge/gpu/gpu_buffer.hpp"
#include "parallax_forge/gpu/gpu_context.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

bool IsUnavailableHardware(const std::runtime_error& error) {
  return std::string_view(error.what()).starts_with(
      "No hardware Direct3D 12 adapter with DXR tier 1.0 support");
}

}  // namespace

int main() {
  using parallax_forge::gpu::GpuBuffer;
  using parallax_forge::gpu::GpuContext;

  try {
    auto context = GpuContext::Create();
    const std::array expected{
        std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}};
    auto upload = GpuBuffer::Upload(context, expected);
    auto gpu_buffer = GpuBuffer::DefaultUav(context, expected.size());

    auto* commands = context.BeginCommands();
    D3D12_RESOURCE_BARRIER to_copy_destination{};
    to_copy_destination.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy_destination.Transition.pResource = gpu_buffer.resource();
    to_copy_destination.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    to_copy_destination.Transition.StateBefore =
        D3D12_RESOURCE_STATE_COMMON;
    to_copy_destination.Transition.StateAfter =
        D3D12_RESOURCE_STATE_COPY_DEST;
    commands->ResourceBarrier(1, &to_copy_destination);
    commands->CopyBufferRegion(gpu_buffer.resource(), 0, upload.resource(), 0,
                               expected.size());

    D3D12_RESOURCE_BARRIER to_uav = to_copy_destination;
    to_uav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    to_uav.Transition.StateAfter =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commands->ResourceBarrier(1, &to_uav);
    context.ExecuteAndWait();

    auto readback = GpuBuffer::Readback(context, expected.size());
    gpu_buffer.CopyToReadback(context, readback,
                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, expected.size()};
    if (FAILED(readback.resource()->Map(0, &read_range, &mapped))) {
      return 1;
    }
    assert(std::memcmp(mapped, expected.data(), expected.size()) == 0);
    const D3D12_RANGE written_range{0, 0};
    readback.resource()->Unmap(0, &written_range);

    auto moved = std::move(readback);
    assert(readback.resource() == nullptr);
    assert(readback.byte_size() == 0);
    assert(readback.gpu_address() == 0);
    assert(moved.byte_size() == expected.size());

    auto move_assigned = GpuBuffer::Readback(context, expected.size());
    move_assigned = std::move(moved);
    assert(moved.resource() == nullptr);
    assert(moved.byte_size() == 0);
    assert(moved.gpu_address() == 0);
    assert(move_assigned.byte_size() == expected.size());
  } catch (const std::runtime_error& error) {
    if (IsUnavailableHardware(error)) {
      return 77;
    }
    throw;
  }
}
