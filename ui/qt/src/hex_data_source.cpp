// WP-5U4A windowed File and Virtual IDAT Hex sources.

#include "pnga/ui/qt/hex_data_source.h"

#include <utility>

namespace pnga::ui::qt {

namespace {

class FileHexDataSource final : public HexDataSource {
 public:
  explicit FileHexDataSource(
      std::shared_ptr<const pnga::io::IByteSource> source)
      : source_(std::move(source)) {}

  const char* name() const noexcept override { return "File"; }
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

std::shared_ptr<const HexDataSource> make_file_hex_source(
    std::shared_ptr<const pnga::io::IByteSource> source) {
  return std::make_shared<FileHexDataSource>(std::move(source));
}

std::shared_ptr<const HexDataSource> make_idat_hex_source(
    std::shared_ptr<const pnga::io::IByteSource> source,
    const pnga::png_format::VirtualIDATStream& stream) {
  return std::make_shared<IdatHexDataSource>(std::move(source), stream);
}

}  // namespace pnga::ui::qt
