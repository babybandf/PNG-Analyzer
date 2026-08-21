// WP-301 streaming inflate over a generic byte source (public zlib API).

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "pnga/deflate-runtime/inflate.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace pnga::deflate_runtime {

namespace {

constexpr std::size_t kInputChunk = 64 * 1024;
constexpr std::size_t kOutputChunk = 64 * 1024;

}  // namespace

InflateOutcome inflate_stream(const pnga::io::IByteSource& source,
                              std::uint64_t max_output) {
  InflateOutcome outcome;

  z_stream zs{};
  if (inflateInit(&zs) != Z_OK) {
    outcome.error = "zlib: inflateInit failed";
    return outcome;
  }

  std::array<std::byte, kInputChunk> inbuf;
  std::array<std::byte, kOutputChunk> outbuf;
  std::uint64_t read_pos = 0;
  bool input_done = false;

  while (true) {
    // Refill the input window from the source when exhausted.
    if (zs.avail_in == 0 && !input_done) {
      const std::uint64_t remaining =
          source.size() > read_pos ? source.size() - read_pos : 0;
      const std::size_t take = static_cast<std::size_t>(
          std::min<std::uint64_t>(remaining, inbuf.size()));
      if (take != 0) {
        source.read(read_pos, inbuf.data(), take);
        read_pos += take;
        zs.next_in = reinterpret_cast<Bytef*>(inbuf.data());
        zs.avail_in = static_cast<uInt>(take);
      } else {
        input_done = true;
      }
    }

    zs.next_out = reinterpret_cast<Bytef*>(outbuf.data());
    zs.avail_out = static_cast<uInt>(outbuf.size());
    const int rc = inflate(&zs, Z_NO_FLUSH);
    const std::size_t produced =
        outbuf.size() - static_cast<std::size_t>(zs.avail_out);

    if (outcome.output.size() + produced > max_output) {
      outcome.output_capped = true;
      outcome.success = false;
      outcome.error = "inflate: output exceeds the size limit";
      inflateEnd(&zs);
      return outcome;
    }
    outcome.output.insert(outcome.output.end(), outbuf.begin(),
                          outbuf.begin() + produced);

    if (rc == Z_STREAM_END) {
      outcome.stream_ended = true;
      outcome.adler_ok = true;
      outcome.success = true;
      inflateEnd(&zs);
      return outcome;
    }
    if (rc == Z_DATA_ERROR || rc == Z_NEED_DICT || rc == Z_MEM_ERROR) {
      outcome.success = false;
      outcome.adler_ok = false;
      outcome.error = rc == Z_DATA_ERROR ? "zlib: corrupt stream or bad checksum"
                                         : "zlib: inflate failed";
      inflateEnd(&zs);
      return outcome;
    }
    if (rc == Z_BUF_ERROR && input_done) {
      // No input left and no more progress: the stream is truncated.
      outcome.success = false;
      outcome.truncated = true;
      outcome.error = "inflate: stream truncated before end";
      inflateEnd(&zs);
      return outcome;
    }
    // Z_OK / Z_BUF_ERROR with input still pending: continue.
  }
}

}  // namespace pnga::deflate_runtime
