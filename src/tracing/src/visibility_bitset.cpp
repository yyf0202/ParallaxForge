#include <parallax_forge/tracing/visibility_bitset.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace parallax_forge::tracing {

std::uint32_t VisibilityWordsPerObject(
    std::uint32_t probe_count) noexcept {
  return probe_count / 32u + (probe_count % 32u == 0 ? 0u : 1u);
}

std::uint64_t CheckedVisibilityWordCount(
    std::uint64_t object_count, std::uint32_t probe_count) {
  const std::uint64_t words_per_object =
      VisibilityWordsPerObject(probe_count);
  if (words_per_object != 0 &&
      object_count >
          (std::numeric_limits<std::uint64_t>::max)() /
              words_per_object) {
    throw std::length_error("Visibility word count overflows uint64.");
  }
  return object_count * words_per_object;
}

std::uint64_t CheckedVisibilityByteCount(
    std::uint64_t object_count, std::uint32_t probe_count) {
  const std::uint64_t word_count =
      CheckedVisibilityWordCount(object_count, probe_count);
  if (word_count >
      (std::numeric_limits<std::uint64_t>::max)() /
          sizeof(std::uint32_t)) {
    throw std::length_error("Visibility byte count overflows uint64.");
  }
  return word_count * sizeof(std::uint32_t);
}

std::uint32_t MaximumVisibilityProbeBatch(
    std::uint64_t object_count) {
  if (object_count == 0) {
    return (std::numeric_limits<std::uint32_t>::max)();
  }
  const std::uint64_t maximum_words_per_object =
      (std::numeric_limits<std::uint32_t>::max)() / object_count;
  if (maximum_words_per_object == 0) {
    throw std::length_error(
        "Object count exceeds the visibility word-address range.");
  }
  const std::uint64_t maximum_probes =
      maximum_words_per_object * 32u;
  return static_cast<std::uint32_t>(
      (std::min)(
          maximum_probes,
          static_cast<std::uint64_t>(
              (std::numeric_limits<std::uint32_t>::max)())));
}

std::uint64_t CheckedVisibilityWordIndex(
    world::InstanceSlot object_slot, std::uint64_t object_count,
    std::uint32_t local_probe, std::uint32_t probe_count) {
  if (object_slot >= object_count) {
    throw std::out_of_range(
        "Visibility object slot is outside the object count.");
  }
  if (local_probe >= probe_count) {
    throw std::out_of_range(
        "Visibility probe is outside the current batch.");
  }
  const std::uint64_t words_per_object =
      VisibilityWordsPerObject(probe_count);
  return static_cast<std::uint64_t>(object_slot) * words_per_object +
      local_probe / 32u;
}

std::uint32_t VisibilityProbeMask(
    std::uint32_t local_probe) noexcept {
  return 1u << (local_probe % 32u);
}

std::vector<world::ObjectId> DecodeVisibilityWord(
    std::span<const std::uint32_t> words, std::uint32_t local_probe,
    const world::ObjectRegistry& registry) {
  const std::size_t object_count = registry.Objects().size();
  if (object_count == 0) {
    if (!words.empty()) {
      throw std::invalid_argument(
          "Visibility words require registered objects.");
    }
    return {};
  }
  if (words.size() % object_count != 0) {
    throw std::invalid_argument(
        "Visibility words are not divisible by the object count.");
  }

  const std::size_t words_per_object = words.size() / object_count;
  const std::size_t probe_word = local_probe / 32u;
  if (probe_word >= words_per_object) {
    throw std::out_of_range(
        "Visibility probe is outside the supplied words.");
  }

  const std::uint32_t mask = VisibilityProbeMask(local_probe);
  std::vector<world::ObjectId> visible_ids;
  visible_ids.reserve(object_count);
  for (world::InstanceSlot slot = 0; slot < object_count; ++slot) {
    const std::size_t word_index =
        static_cast<std::size_t>(slot) * words_per_object + probe_word;
    if ((words[word_index] & mask) != 0) {
      visible_ids.push_back(registry.ObjectFor(slot).object_id);
    }
  }
  return visible_ids;
}

}  // namespace parallax_forge::tracing
