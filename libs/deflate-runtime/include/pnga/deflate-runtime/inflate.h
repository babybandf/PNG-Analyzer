#ifndef PNGA_DEFLATE_RUNTIME_INFLATE_H
#define PNGA_DEFLATE_RUNTIME_INFLATE_H

// WP-301: streaming zlib inflate over a generic byte source using public zlib
// APIs only. Bounded output (decompression-bomb protection), Adler-32 /
// checksum reporting and truncation detection. The module knows nothing about
// PNG chunks or scanlines.

#include "pnga/io/byte_source.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::deflate_runtime {

struct InflateOutcome {
  bool success = false;
  std::string error;              // stable message on failure
  std::vector<std::byte> output;  // inflated bytes (owned)
  bool adler_ok = true;           // false when zlib reports a checksum error
  bool stream_ended = false;      // Z_STREAM_END was reached
  bool truncated = false;         // input exhausted before the stream ended
  bool output_capped = false;     // output hit `max_output` (possible bomb)
};

// Inflates the zlib stream from `source` (read() only; views are not used).
// Produces at most `max_output` bytes; exceeding it fails with output_capped.
InflateOutcome inflate_stream(const pnga::io::IByteSource& source,
                              std::uint64_t max_output);

}  // namespace pnga::deflate_runtime

#endif  // PNGA_DEFLATE_RUNTIME_INFLATE_H
