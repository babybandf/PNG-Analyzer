#ifndef PNGA_ANALYSIS_ENGINE_SRC_CANCELLABLE_STREAM_H
#define PNGA_ANALYSIS_ENGINE_SRC_CANCELLABLE_STREAM_H

#include <pnga/io/byte_source.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>

namespace pnga::analysis_engine {

class CancellableStream final : public pnga::io::IByteSource {
 public:
  CancellableStream(const pnga::io::IByteSource& source,
                    std::function<bool()> cancelled)
      : source_(source), cancelled_(std::move(cancelled)) {}

  std::uint64_t size() const noexcept override {
    return is_cancelled() ? 0 : source_.size();
  }

  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    if (!is_cancelled()) {
      return source_.read(offset, out, length);
    }
    if (out != nullptr) {
      std::fill_n(out, length, std::byte{0});
    }
    return true;
  }

  std::optional<pnga::io::ByteView> view(
      std::uint64_t, std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  bool is_cancelled() const noexcept {
    return cancelled_ && cancelled_();
  }

  const pnga::io::IByteSource& source_;
  std::function<bool()> cancelled_;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_SRC_CANCELLABLE_STREAM_H
