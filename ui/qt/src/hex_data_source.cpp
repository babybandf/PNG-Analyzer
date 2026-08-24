// WP-5U4A windowed File and Virtual IDAT Hex sources.

#include "pnga/ui/qt/hex_data_source.h"

#include <cstring>
#include <utility>

namespace pnga::ui::qt {

namespace {

const char* status_text(HexDataStatus status) noexcept {
  switch (status) {
    case HexDataStatus::kReady:
      return "ready";
    case HexDataStatus::kUnavailable:
      return "unavailable";
    case HexDataStatus::kReplaying:
      return "replaying";
    case HexDataStatus::kError:
      return "error";
  }
  return "error";
}

class FileHexDataSource final : public HexDataSource {
 public:
  explicit FileHexDataSource(
      std::shared_ptr<const pnga::io::IByteSource> source)
      : source_(std::move(source)) {}

  const char* name() const noexcept override { return "File"; }
  HexDataStatus status() const noexcept override {
    return source_ == nullptr ? HexDataStatus::kUnavailable
                               : HexDataStatus::kReady;
  }
  std::uint64_t size() const noexcept override {
    return source_ == nullptr ? 0 : source_->size();
  }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return source_ != nullptr && source_->read(offset, out, length);
  }

 private:
  std::shared_ptr<const pnga::io::IByteSource> source_;
};

class IdatHexDataSource final : public HexDataSource {
 public:
  IdatHexDataSource(std::shared_ptr<const pnga::io::IByteSource> source,
                    pnga::png_format::VirtualIDATStream stream)
      : source_(std::move(source)), stream_(std::move(stream)) {}

  const char* name() const noexcept override { return "IDAT Stream"; }
  HexDataStatus status() const noexcept override {
    return source_ == nullptr ? HexDataStatus::kUnavailable
                               : HexDataStatus::kReady;
  }
  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return source_ != nullptr && stream_.read(*source_, offset, out, length);
  }

 private:
  std::shared_ptr<const pnga::io::IByteSource> source_;
  pnga::png_format::VirtualIDATStream stream_;
};

}  // namespace

const char* hex_data_status_text(HexDataStatus status) noexcept {
  return status_text(status);
}

std::shared_ptr<const HexDataSource> make_file_hex_source(
    std::shared_ptr<const pnga::io::IByteSource> source) {
  return std::make_shared<FileHexDataSource>(std::move(source));
}

std::shared_ptr<const HexDataSource> make_idat_hex_source(
    std::shared_ptr<const pnga::io::IByteSource> source,
    const pnga::png_format::VirtualIDATStream& stream) {
  return std::make_shared<IdatHexDataSource>(std::move(source), stream);
}

class StageBytesHexDataSource final : public HexDataSource {
 public:
  StageBytesHexDataSource(
      const char* name,
      std::shared_ptr<const pnga::analysis_engine::StageSet> stages,
      bool defiltered)
      : name_(name), stages_(std::move(stages)), defiltered_(defiltered) {}

  const char* name() const noexcept override { return name_; }
  HexDataStatus status() const noexcept override {
    if (stages_ == nullptr) {
      return HexDataStatus::kUnavailable;
    }
    return stages_->success ? HexDataStatus::kReady : HexDataStatus::kError;
  }
  std::uint64_t size() const noexcept override {
    if (status() != HexDataStatus::kReady) {
      return 0;
    }
    return static_cast<std::uint64_t>(data().size());
  }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    if (status() != HexDataStatus::kReady || offset > size() ||
        length > size() - offset || (out == nullptr && length != 0)) {
      return false;
    }
    if (length != 0) {
      std::memcpy(out, data().data() + static_cast<std::size_t>(offset),
                  length);
    }
    return true;
  }

 private:
  const std::vector<std::byte>& data() const noexcept {
    return defiltered_ ? stages_->unfiltered : stages_->filtered;
  }

  const char* name_;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stages_;
  bool defiltered_;
};

std::shared_ptr<const HexDataSource> make_inflated_hex_source(
    std::shared_ptr<const pnga::analysis_engine::StageSet> stages) {
  return std::make_shared<StageBytesHexDataSource>("Inflated",
                                                   std::move(stages), false);
}

std::shared_ptr<const HexDataSource> make_defiltered_hex_source(
    std::shared_ptr<const pnga::analysis_engine::StageSet> stages) {
  return std::make_shared<StageBytesHexDataSource>("Unfiltered",
                                                   std::move(stages), true);
}

}  // namespace pnga::ui::qt
