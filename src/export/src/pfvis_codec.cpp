#include <parallax_forge/export/pfvis_codec.hpp>

#include "transactional_output.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace parallax_forge::export_data {
namespace {

constexpr std::string_view kMagic = "PFV1";
constexpr std::uint16_t kVersion = 2;
constexpr std::uint16_t kFlags = 0;

void WriteByte(std::ofstream& file, std::uint8_t value) {
  file.put(static_cast<char>(value));
  if (!file) {
    throw std::runtime_error("failed to write PFVIS output");
  }
}

void WriteU16(std::ofstream& file, std::uint16_t value) {
  WriteByte(file, static_cast<std::uint8_t>(value));
  WriteByte(file, static_cast<std::uint8_t>(value >> 8));
}

void WriteU32(std::ofstream& file, std::uint32_t value) {
  for (std::uint32_t shift = 0; shift < 32; shift += 8) {
    WriteByte(file, static_cast<std::uint8_t>(value >> shift));
  }
}

void WriteFloat(std::ofstream& file, float value) {
  WriteU32(file, std::bit_cast<std::uint32_t>(value));
}

void WriteString(std::ofstream& file, std::string_view value) {
  file.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!file) {
    throw std::runtime_error("failed to write PFVIS output");
  }
}

std::uint8_t ReadByte(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
  if (offset == bytes.size()) {
    throw std::runtime_error("truncated PFVIS input");
  }
  return bytes[offset++];
}

std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
  std::uint16_t value = 0;
  for (std::uint16_t shift = 0; shift < 16; shift += 8) {
    value |= static_cast<std::uint16_t>(ReadByte(bytes, offset)) << shift;
  }
  return value;
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
  std::uint32_t value = 0;
  for (std::uint32_t shift = 0; shift < 32; shift += 8) {
    value |= static_cast<std::uint32_t>(ReadByte(bytes, offset)) << shift;
  }
  return value;
}

float ReadFloat(const std::vector<std::uint8_t>& bytes, std::size_t& offset) {
  return std::bit_cast<float>(ReadU32(bytes, offset));
}

std::uint32_t CheckedU32Size(std::size_t size, const char* description) {
  if (size > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error(description);
  }
  return static_cast<std::uint32_t>(size);
}

void RequireElements(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::uint64_t count,
    std::size_t element_size,
    const char* description) {
  if (offset > bytes.size() || count > (bytes.size() - offset) / element_size) {
    throw std::runtime_error(description);
  }
}

std::string ReadString(
    const std::vector<std::uint8_t>& bytes,
    std::size_t& offset,
    std::uint32_t length) {
  RequireElements(bytes, offset, length, 1, "truncated PFVIS object label");
  const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
  offset += static_cast<std::size_t>(length);
  return {begin, begin + static_cast<std::ptrdiff_t>(length)};
}

struct EncodedProbe {
  ProbeId probe_id;
  world::Vec3 position;
  std::uint32_t visible_offset;
  std::uint32_t visible_count;
};

}  // namespace

void WritePfvis(const VisibilityCatalog& catalog, const std::filesystem::path& output_path) {
  const auto object_count = CheckedU32Size(catalog.Objects().size(), "too many PFVIS objects");
  const auto probe_count = CheckedU32Size(catalog.Probes().size(), "too many PFVIS probes");

  std::vector<EncodedProbe> probes;
  probes.reserve(catalog.Probes().size());
  std::vector<world::ObjectId> visible_object_ids;
  for (const auto& probe : catalog.Probes()) {
    const auto visible_offset = CheckedU32Size(visible_object_ids.size(), "too many PFVIS visible objects");
    const auto visible_count = CheckedU32Size(probe.visible_object_ids.size(), "too many PFVIS visible objects");
    visible_object_ids.insert(
        visible_object_ids.end(), probe.visible_object_ids.begin(), probe.visible_object_ids.end());
    probes.push_back({probe.probe_id, probe.position, visible_offset, visible_count});
  }

  detail::TransactionalOutput output(output_path);
  std::ofstream file(output.TemporaryPath(), std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("failed to open temporary PFVIS output");
  }

  for (const auto byte : kMagic) {
    WriteByte(file, static_cast<std::uint8_t>(byte));
  }
  WriteU16(file, kVersion);
  WriteU16(file, kFlags);
  WriteU32(file, object_count);
  WriteU32(file, probe_count);
  for (const auto& object : catalog.Objects()) {
    WriteU32(file, object.object_id);
    WriteU32(file, CheckedU32Size(object.label.size(), "PFVIS object label is too large"));
    WriteString(file, object.label);
  }
  for (const auto& probe : probes) {
    WriteU32(file, probe.probe_id);
    WriteFloat(file, probe.position.x);
    WriteFloat(file, probe.position.y);
    WriteFloat(file, probe.position.z);
    WriteU32(file, probe.visible_offset);
    WriteU32(file, probe.visible_count);
  }
  for (const auto object_id : visible_object_ids) {
    WriteU32(file, object_id);
  }
  file.close();
  if (!file) {
    throw std::runtime_error("failed to close temporary PFVIS output");
  }
  output.Commit();
}

VisibilityCatalog ReadPfvis(const std::filesystem::path& input_path) {
  std::ifstream file(input_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("failed to open PFVIS input");
  }
  const std::vector<std::uint8_t> bytes{
      std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  if (file.bad()) {
    throw std::runtime_error("failed to read PFVIS input");
  }

  std::size_t offset = 0;
  for (const auto expected : kMagic) {
    if (ReadByte(bytes, offset) != static_cast<std::uint8_t>(expected)) {
      throw std::runtime_error("invalid PFVIS magic");
    }
  }
  if (ReadU16(bytes, offset) != kVersion) {
    throw std::runtime_error("unsupported PFVIS version");
  }
  if (ReadU16(bytes, offset) != kFlags) {
    throw std::runtime_error("unsupported PFVIS flags");
  }

  const auto object_count = ReadU32(bytes, offset);
  const auto probe_count = ReadU32(bytes, offset);
  RequireElements(bytes, offset, object_count, 8, "truncated PFVIS object data");
  std::vector<world::ObjectDefinition> objects;
  objects.reserve(object_count);
  for (std::uint32_t index = 0; index < object_count; ++index) {
    RequireElements(bytes, offset, 1, 8, "truncated PFVIS object data");
    const auto object_id = ReadU32(bytes, offset);
    const auto label_length = ReadU32(bytes, offset);
    objects.push_back({
        object_id,
        ReadString(bytes, offset, label_length),
        {},
        world::Transform::Identity(),
    });
  }

  RequireElements(bytes, offset, probe_count, 24, "truncated PFVIS probe data");
  std::vector<EncodedProbe> probes;
  probes.reserve(probe_count);
  std::uint64_t visible_count = 0;
  for (std::uint32_t index = 0; index < probe_count; ++index) {
    EncodedProbe probe{
        ReadU32(bytes, offset),
        {ReadFloat(bytes, offset), ReadFloat(bytes, offset), ReadFloat(bytes, offset)},
        ReadU32(bytes, offset),
        ReadU32(bytes, offset),
    };
    visible_count += probe.visible_count;
    if (visible_count > std::numeric_limits<std::size_t>::max()) {
      throw std::runtime_error("too many PFVIS visible objects");
    }
    probes.push_back(probe);
  }

  RequireElements(bytes, offset, visible_count, 4, "truncated PFVIS visible object data");
  const auto visible_object_count = static_cast<std::size_t>(visible_count);
  for (const auto& probe : probes) {
    const auto visible_offset = static_cast<std::size_t>(probe.visible_offset);
    const auto count = static_cast<std::size_t>(probe.visible_count);
    if (visible_offset > visible_object_count || count > visible_object_count - visible_offset) {
      throw std::runtime_error("PFVIS visible object range is out of bounds");
    }
  }
  std::vector<world::ObjectId> visible_object_ids;
  visible_object_ids.reserve(visible_object_count);
  for (std::uint64_t index = 0; index < visible_count; ++index) {
    visible_object_ids.push_back(ReadU32(bytes, offset));
  }
  if (offset != bytes.size()) {
    throw std::runtime_error("trailing bytes in PFVIS input");
  }

  VisibilityCatalog catalog(std::move(objects));
  try {
    for (const auto& probe : probes) {
      const auto visible_offset = static_cast<std::size_t>(probe.visible_offset);
      const auto count = static_cast<std::size_t>(probe.visible_count);
      catalog.AddProbe({
          probe.probe_id,
          probe.position,
          std::vector<world::ObjectId>(
              visible_object_ids.begin() + static_cast<std::ptrdiff_t>(visible_offset),
              visible_object_ids.begin() + static_cast<std::ptrdiff_t>(visible_offset + count)),
      });
    }
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("invalid PFVIS catalog data");
  }
  return catalog;
}

}  // namespace parallax_forge::export_data
