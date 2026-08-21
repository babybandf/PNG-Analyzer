// WP-301: inflate the virtual IDAT stream and split filtered scanlines.

#include "pnga/analysis-engine/filtered_scanlines.h"

#include <pnga/deflate-runtime/inflate.h>

#include "virtual_idat_source.h"

#include <algorithm>
#include <cstdint>

namespace pnga::analysis_engine {

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
