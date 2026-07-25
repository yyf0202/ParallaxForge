#include "parallax_forge/gpu/gpu_context.hpp"

#include "gpu_context_features.hpp"

#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace parallax_forge::gpu {
namespace {

using Microsoft::WRL::ComPtr;

[[noreturn]] void ThrowHresult(const char* operation, HRESULT result) {
  std::ostringstream message;
  message << operation << " failed with HRESULT 0x" << std::hex
          << std::uppercase << static_cast<std::uint32_t>(result);
  throw std::runtime_error(message.str());
}

void ThrowIfFailed(HRESULT result, const char* operation) {
  if (FAILED(result)) {
    ThrowHresult(operation, result);
  }
}

std::uint64_t CompletedFenceValueOrThrow(ID3D12Fence* fence,
                                         ID3D12Device* device) {
  const std::uint64_t completed_value = fence->GetCompletedValue();
  if (completed_value !=
      (std::numeric_limits<std::uint64_t>::max)()) {
    return completed_value;
  }

  const HRESULT removal_reason = device->GetDeviceRemovedReason();
  if (FAILED(removal_reason)) {
    ThrowHresult("Direct3D 12 device removal", removal_reason);
  }
  throw std::runtime_error(
      "Direct3D 12 device was removed without a failing removal reason.");
}

ComPtr<ID3D12Device5> CreateDxrDevice() {
  ComPtr<IDXGIFactory6> factory;
  ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)),
                "CreateDXGIFactory2");

  for (UINT adapter_index = 0;; ++adapter_index) {
    ComPtr<IDXGIAdapter1> adapter;
    const HRESULT enum_result = factory->EnumAdapterByGpuPreference(
        adapter_index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&adapter));
    if (enum_result == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    ThrowIfFailed(enum_result, "IDXGIFactory6::EnumAdapterByGpuPreference");

    DXGI_ADAPTER_DESC1 description{};
    ThrowIfFailed(adapter->GetDesc1(&description), "IDXGIAdapter1::GetDesc1");
    if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
      continue;
    }

    ComPtr<ID3D12Device5> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                 IID_PPV_ARGS(&device)))) {
      continue;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    if (SUCCEEDED(device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) &&
        detail::SupportsRequiredGpuFeatures(options.RaytracingTier)) {
      return device;
    }
  }

  throw std::runtime_error(
      "No hardware Direct3D 12 adapter with DXR tier 1.0 support was found.");
}

}  // namespace

bool detail::SupportsRequiredGpuFeatures(
    D3D12_RAYTRACING_TIER raytracing_tier) noexcept {
  return raytracing_tier >= D3D12_RAYTRACING_TIER_1_0;
}

struct GpuContext::Impl {
  ~Impl() {
    if (fence_event != nullptr) {
      CloseHandle(fence_event);
    }
  }

  ComPtr<ID3D12Device5> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList4> command_list;
  HANDLE fence_event = nullptr;
  std::uint64_t next_fence_value = 1;
  bool recording = false;
};

GpuContext::GpuContext(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

GpuContext GpuContext::Create() {
  auto impl = std::make_unique<Impl>();
  impl->device = CreateDxrDevice();

  D3D12_COMMAND_QUEUE_DESC queue_description{};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_description.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queue_description.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queue_description.NodeMask = 0;
  ThrowIfFailed(
      impl->device->CreateCommandQueue(&queue_description,
                                       IID_PPV_ARGS(&impl->queue)),
      "ID3D12Device::CreateCommandQueue");
  ThrowIfFailed(
      impl->device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                IID_PPV_ARGS(&impl->fence)),
      "ID3D12Device::CreateFence");

  impl->fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (impl->fence_event == nullptr) {
    ThrowHresult("CreateEventW", HRESULT_FROM_WIN32(GetLastError()));
  }

  ThrowIfFailed(impl->device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&impl->allocator)),
                "ID3D12Device::CreateCommandAllocator");
  ThrowIfFailed(
      impl->device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, impl->allocator.Get(), nullptr,
          IID_PPV_ARGS(&impl->command_list)),
      "ID3D12Device::CreateCommandList");
  ThrowIfFailed(impl->command_list->Close(),
                "ID3D12GraphicsCommandList::Close");

  return GpuContext(std::move(impl));
}

GpuContext::GpuContext(GpuContext&&) noexcept = default;
GpuContext& GpuContext::operator=(GpuContext&&) noexcept = default;
GpuContext::~GpuContext() = default;

bool GpuContext::SupportsDxr() const noexcept {
  return impl_ != nullptr && impl_->device != nullptr;
}

ID3D12Device5* GpuContext::device() const noexcept {
  return impl_ == nullptr ? nullptr : impl_->device.Get();
}

ID3D12GraphicsCommandList4* GpuContext::BeginCommands() {
  if (impl_ == nullptr) {
    throw std::logic_error("Cannot record commands with a moved-from GpuContext.");
  }
  if (impl_->recording) {
    throw std::logic_error("A GPU command list is already being recorded.");
  }

  ThrowIfFailed(impl_->allocator->Reset(),
                "ID3D12CommandAllocator::Reset");
  ThrowIfFailed(impl_->command_list->Reset(impl_->allocator.Get(), nullptr),
                "ID3D12GraphicsCommandList::Reset");
  impl_->recording = true;
  return impl_->command_list.Get();
}

void GpuContext::ExecuteAndWait() {
  if (impl_ == nullptr) {
    throw std::logic_error("Cannot execute commands with a moved-from GpuContext.");
  }
  if (!impl_->recording) {
    throw std::logic_error("No GPU command list is being recorded.");
  }

  ThrowIfFailed(impl_->command_list->Close(),
                "ID3D12GraphicsCommandList::Close");
  impl_->recording = false;

  ID3D12CommandList* command_lists[] = {impl_->command_list.Get()};
  impl_->queue->ExecuteCommandLists(1, command_lists);

  const std::uint64_t fence_value = impl_->next_fence_value++;
  ThrowIfFailed(impl_->queue->Signal(impl_->fence.Get(), fence_value),
                "ID3D12CommandQueue::Signal");
  if (CompletedFenceValueOrThrow(impl_->fence.Get(), impl_->device.Get()) <
      fence_value) {
    ThrowIfFailed(
        impl_->fence->SetEventOnCompletion(fence_value, impl_->fence_event),
        "ID3D12Fence::SetEventOnCompletion");
    const DWORD wait_result = WaitForSingleObject(impl_->fence_event, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
      ThrowHresult("WaitForSingleObject",
                   HRESULT_FROM_WIN32(GetLastError()));
    }
    if (CompletedFenceValueOrThrow(impl_->fence.Get(), impl_->device.Get()) <
        fence_value) {
      throw std::runtime_error(
          "GPU fence event was signaled before the requested value completed.");
    }
  }
}

}  // namespace parallax_forge::gpu
