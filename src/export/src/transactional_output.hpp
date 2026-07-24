#pragma once

#include <filesystem>

namespace parallax_forge::export_data::detail {

class TransactionalOutput {
 public:
  explicit TransactionalOutput(std::filesystem::path target_path);
  ~TransactionalOutput();

  TransactionalOutput(const TransactionalOutput&) = delete;
  TransactionalOutput& operator=(const TransactionalOutput&) = delete;

  [[nodiscard]] const std::filesystem::path& TemporaryPath() const noexcept;
  void Commit();

 private:
  std::filesystem::path target_path_;
  std::filesystem::path temporary_path_;
  bool committed_{};
};

}  // namespace parallax_forge::export_data::detail
