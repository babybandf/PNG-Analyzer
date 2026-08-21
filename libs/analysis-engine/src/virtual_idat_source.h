#ifndef PNGA_ANALYSIS_ENGINE_SRC_VIRTUAL_IDAT_SOURCE_H
#define PNGA_ANALYSIS_ENGINE_SRC_VIRTUAL_IDAT_SOURCE_H

// Internal: adapts a VirtualIDATStream to IByteSource for the generic inflate
// and deflate-index layers. Only read() is meaningful; there is no contiguous
// backing to view. Shared by the inflate and scanline-anchor orchestrations.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <optional>

namespace pnga::analysis_engine {

class VirtualIdatSource final : public pnga::io::IByteSource {
 public:
  VirtualIdatSource(const pnga::png_format::VirtualIDATStream& stream,
                    const pnga::io::IByteSource& file)
      : stream_(stream), file_(file) {}

  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(std::uint64_t,
                                         std::size_t) const noexcept override {
    return std::nullopt;  // non-contiguous
  }

 private:
  const pnga::png_format::VirtualIDATStream& stream_;
  const pnga::io::IByteSource& file_;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_SRC_VIRTUAL_IDAT_SOURCE_H
