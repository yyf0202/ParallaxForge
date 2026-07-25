#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <parallax_forge/world/object_registry.hpp>

namespace parallax_forge::tracing {

[[nodiscard]] std::uint32_t VisibilityWordsPerObject(
    std::uint32_t probe_count) noexcept;

[[nodiscard]] std::uint64_t CheckedVisibilityWordCount(
    std::uint64_t object_count, std::uint32_t probe_count);

[[nodiscard]] std::uint64_t CheckedVisibilityByteCount(
    std::uint64_t object_count, std::uint32_t probe_count);

[[nodiscard]] std::uint32_t MaximumVisibilityProbeBatch(
    std::uint64_t object_count);

[[nodiscard]] std::uint64_t CheckedVisibilityWordIndex(
    world::InstanceSlot object_slot, std::uint64_t object_count,
    std::uint32_t local_probe, std::uint32_t probe_count);

[[nodiscard]] std::uint32_t VisibilityProbeMask(
    std::uint32_t local_probe) noexcept;

[[nodiscard]] std::vector<world::ObjectId> DecodeVisibilityWord(
    std::span<const std::uint32_t> words, std::uint32_t local_probe,
    const world::ObjectRegistry& registry);

}  // namespace parallax_forge::tracing
