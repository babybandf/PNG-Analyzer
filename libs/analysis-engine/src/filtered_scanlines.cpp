// WP-301: inflate the virtual IDAT stream and split filtered scanlines.

#include "pnga/analysis-engine/filtered_scanlines.h"

#include <pnga/deflate-runtime/inflate.h>

#include <algorithm>
#include <cstdint>

namespace pnga::analysis_engine {

namespace {

// Adapts a VirtualIDATStream to IByteSource for the generic inflate wrapper.
// Only read() is meaningful; there is no contiguous backing to view.
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

}  // namespace

FilteredOutcome inflate_filtered(
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source,
    const pnga::png_reconstruction::ScanlineLayout& layout) {
  FilteredOutcome outcome;

  const std::uint64_t expected =
      layout.total_bytes.value_or(0);
  VirtualIdatSource adapter(stream, source);
  const deflate_runtime::InflateOutcome inf =
      deflate_runtime::inflate_stream(adapter, expected);

  outcome.adler_ok = inf.adler_ok;
  if (!inf.success) {
    outcome.error = inf.error;
    return outcome;
  }
  if (!inf.stream_ended) {
    outcome.error = "inflate: stream did not reach end";
    return outcome;
  }

  outcome.filtered = std::move(inf.output);
  outcome.exact_size = outcome.filtered.size() == expected;
  if (!outcome.exact_size) {
    outcome.error = "inflate: inflated size does not match the scanline layout";
    return outcome;
  }

  // Split into scanlines in stream order (Adam7 pass-major, row-minor).
  std::uint64_t offset = 0;
  for (std::size_t p = 0; p < layout.pass_count; ++p) {
    const auto& pass = layout.passes[p];
    for (std::uint64_t row = 0; row < pass.height; ++row) {
      outcome.scanlines.push_back(
          FilteredScanlineSpan{offset, pass.filter_row_bytes});
      offset += pass.filter_row_bytes;
    }
  }

  outcome.success = true;
  return outcome;
}

}  // namespace pnga::analysis_engine
