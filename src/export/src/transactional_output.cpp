#include "transactional_output.hpp"

#include <system_error>
#include <utility>

#define NOMINMAX
#include <Windows.h>

namespace parallax_forge::export_data::detail {

TransactionalOutput::TransactionalOutput(std::filesystem::path target_path)
    : target_path_(std::move(target_path)),
      temporary_path_(target_path_) {
  temporary_path_ += ".tmp";
}

TransactionalOutput::~TransactionalOutput() {
  if (!committed_) {
    std::error_code ignored;
    std::filesystem::remove(temporary_path_, ignored);
  }
}

const std::filesystem::path& TransactionalOutput::TemporaryPath() const noexcept {
  return temporary_path_;
}

void TransactionalOutput::Commit() {
  if (!MoveFileExW(
          temporary_path_.c_str(), target_path_.c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::filesystem::filesystem_error(
        "failed to replace visibility output", temporary_path_, target_path_,
        std::error_code(static_cast<int>(GetLastError()), std::system_category()));
  }
  committed_ = true;
}

}  // namespace parallax_forge::export_data::detail
