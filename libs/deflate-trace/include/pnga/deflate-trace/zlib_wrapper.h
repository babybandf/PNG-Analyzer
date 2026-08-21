#ifndef PNGA_DEFLATE_TRACE_ZLIB_WRAPPER_H
#define PNGA_DEFLATE_TRACE_ZLIB_WRAPPER_H

// WP-500: transparent zlib wrapper trace (RFC 1950). Parses the CMF/FLG header
// (compression method CINFO, FCHECK, FDICT, FLEVEL), the optional DICTID and
// the trailing Adler-32 into trace-model field nodes with bit spans, fully
// independent of zlib. Structural validity is decided here (method, check,
// window); Adler-32 and Deflate-data verification are cross-checked by callers
// with standard zlib (this module is zlib-free per layout §7).

#include <pnga/io/byte_source.h>
#include <pnga/trace-model/selection.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pnga::deflate_trace {

// One parsed wrapper field with its span in the logical stream.
struct WrapperField {
  pnga::trace_model::BitSpan span;  // bit-aligned for sub-byte fields
  std::string name;
  std::string text;   // human-readable value
  std::uint64_t value = 0;
};

struct ZlibWrapperTrace {
  bool success = false;
  std::string error;  // stable message when the wrapper is structurally invalid
  std::vector<WrapperField> fields;  // CMF/CM/CINFO/FLG/FCHECK/FDICT/FLEVEL
                                     // [+DICTID] [+ADLER32]

  std::uint8_t cmf = 0;
  std::uint8_t flg = 0;
  std::uint8_t cm = 0;      // compression method (8 = deflate)
  std::uint8_t cinfo = 0;   // window size 2^(cinfo + 8)
  bool fcheck_ok = true;    // (cmf*256 + flg) mod 31 == 0
  bool fdict = false;
  std::uint8_t flevel = 0;
  std::optional<std::uint32_t> dictid;   // present when fdict
  std::uint64_t deflate_data_begin = 0;  // byte offset after header/DICTID

  // Trailing Adler-32 (the last 4 bytes of the stream).
  std::optional<std::uint64_t> adler_offset;  // byte offset
  std::optional<std::uint32_t> adler_value;   // stored big-endian value
  std::uint64_t total_bytes = 0;
};

// Parses the zlib wrapper and trailing Adler-32 of the logical stream
// `source` (read() only). Fails with a stable error when the stream is too
// short, the method is not deflate, FCHECK fails, CINFO exceeds the zlib
// window limit, DICTID is missing, or there is no room for the Adler-32.
ZlibWrapperTrace trace_zlib_wrapper(const pnga::io::IByteSource& source);

}  // namespace pnga::deflate_trace

#endif  // PNGA_DEFLATE_TRACE_ZLIB_WRAPPER_H
