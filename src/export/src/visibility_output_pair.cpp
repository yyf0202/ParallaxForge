#include <parallax_forge/export/visibility_output_pair.hpp>

#include <parallax_forge/export/json_visibility_writer.hpp>
#include <parallax_forge/export/pfvis_codec.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#define NOMINMAX
#include <Windows.h>

namespace parallax_forge::export_data {
namespace {

struct OutputEntry {
  std::filesystem::path target;
  std::filesystem::path staged;
  std::filesystem::path backup;
  bool backup_created{};
  bool replacement_created{};
};

std::uint64_t NextTransactionId() noexcept {
  static std::atomic_uint64_t next_id{1};
  return next_id.fetch_add(1, std::memory_order_relaxed);
}

std::filesystem::path SiblingPath(
    const std::filesystem::path& target,
    const wchar_t* role,
    std::uint64_t transaction_id) {
  auto result = target;
  result += L".pair-";
  result += role;
  result += L"-";
  result += std::to_wstring(GetCurrentProcessId());
  result += L"-";
  result += std::to_wstring(transaction_id);
  return result;
}

[[noreturn]] void ThrowMoveError(
    const char* operation,
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
  throw std::filesystem::filesystem_error(
      operation, source, destination,
      std::error_code(static_cast<int>(GetLastError()),
                      std::system_category()));
}

void MoveFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    DWORD flags,
    const char* operation) {
  if (!MoveFileExW(source.c_str(), destination.c_str(), flags)) {
    ThrowMoveError(operation, source, destination);
  }
}

void RemoveFile(const std::filesystem::path& path) noexcept {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

class OutputPairTransaction {
 public:
  OutputPairTransaction(
      std::filesystem::path json_path,
      std::filesystem::path pfvis_path) {
    const std::uint64_t transaction_id = NextTransactionId();
    entries_[0] = OutputEntry{
        json_path,
        SiblingPath(json_path, L"stage", transaction_id),
        SiblingPath(json_path, L"backup", transaction_id)};
    entries_[1] = OutputEntry{
        pfvis_path,
        SiblingPath(pfvis_path, L"stage", transaction_id),
        SiblingPath(pfvis_path, L"backup", transaction_id)};
  }

  ~OutputPairTransaction() {
    for (const auto& entry : entries_) {
      RemoveFile(entry.staged);
      if (!entry.backup_created) {
        RemoveFile(entry.backup);
      }
    }
  }

  OutputPairTransaction(const OutputPairTransaction&) = delete;
  OutputPairTransaction& operator=(const OutputPairTransaction&) = delete;

  [[nodiscard]] const std::filesystem::path& JsonStage() const noexcept {
    return entries_[0].staged;
  }

  [[nodiscard]] const std::filesystem::path& PfvisStage() const noexcept {
    return entries_[1].staged;
  }

  void Commit() {
    try {
      for (auto& entry : entries_) {
        if (std::filesystem::exists(entry.target)) {
          MoveFile(entry.target, entry.backup, MOVEFILE_WRITE_THROUGH,
                   "failed to back up visibility output pair");
          entry.backup_created = true;
        }
        MoveFile(entry.staged, entry.target, MOVEFILE_WRITE_THROUGH,
                 "failed to replace visibility output pair");
        entry.replacement_created = true;
      }
    } catch (const std::filesystem::filesystem_error& error) {
      const std::error_code rollback_error = Rollback();
      if (rollback_error) {
        throw std::filesystem::filesystem_error(
            "failed to commit visibility output pair and rollback; "
            "original backup was preserved",
            error.path1(), error.path2(), rollback_error);
      }
      throw;
    }

    for (auto& entry : entries_) {
      RemoveFile(entry.backup);
      entry.backup_created = false;
      entry.replacement_created = false;
    }
  }

 private:
  std::error_code Rollback() noexcept {
    std::error_code first_error;
    for (auto entry = entries_.rbegin(); entry != entries_.rend(); ++entry) {
      if (entry->replacement_created) {
        std::error_code remove_error;
        std::filesystem::remove(entry->target, remove_error);
        if (remove_error && !first_error) {
          first_error = remove_error;
        } else if (!remove_error) {
          entry->replacement_created = false;
        }
      }
      if (entry->backup_created && !entry->replacement_created) {
        if (MoveFileExW(
                entry->backup.c_str(), entry->target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
          entry->backup_created = false;
        } else if (!first_error) {
          first_error = std::error_code(
              static_cast<int>(GetLastError()), std::system_category());
        }
      }
    }
    return first_error;
  }

  std::array<OutputEntry, 2> entries_;
};

}  // namespace

void WriteVisibilityOutputPair(
    const VisibilityCatalog& catalog,
    const std::filesystem::path& json_path,
    const std::filesystem::path& pfvis_path) {
  OutputPairTransaction transaction(json_path, pfvis_path);
  WriteJsonVisibility(catalog, transaction.JsonStage());
  WritePfvis(catalog, transaction.PfvisStage());
  transaction.Commit();
}

}  // namespace parallax_forge::export_data
