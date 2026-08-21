// WP-402 access point implementation. A Z_BLOCK scan (as in block_index.cpp)
// also buffers the last 32 KiB of output and drops a restart point at block
// boundaries spaced by the output interval. extract_output() restarts with raw
// inflate (windowBits -15) + dictionary + bit priming from the nearest point.

#include "pnga/deflate-index/access_points.h"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace pnga::deflate_index {

namespace {

constexpr std::size_t kInputChunk = 1 << 16;
constexpr std::size_t kScratchSize = 1 << 16;

// Reads up to 16 bits at bit offset `bit_pos` in Deflate order (LSB-first),
// mirroring block_index.cpp.
std::uint16_t read_bits(const pnga::io::IByteSource& source,
                        std::uint64_t bit_pos, unsigned count) {
  std::byte bytes[3] = {};
  const std::uint64_t byte_pos = bit_pos / 8;
  const std::uint64_t byte_count = std::min<std::uint64_t>(
      3, source.size() > byte_pos ? source.size() - byte_pos : 0);
  if (byte_count != 0) {
    source.read(byte_pos, bytes, static_cast<std::size_t>(byte_count));
  }
  std::uint16_t value = 0;
  for (unsigned k = 0; k < count; ++k) {
    const unsigned shift = static_cast<unsigned>((bit_pos + k) % 8);
    const std::uint16_t bit =
        (static_cast<unsigned>(bytes[k / 8]) >> shift) & 1u;
    value = static_cast<std::uint16_t>(value | (bit << k));
  }
  return value;
}

}  // namespace

AccessIndexResult build_access_index(const pnga::io::IByteSource& source,
                                     std::uint64_t max_output_bytes,
                                     std::uint64_t min_interval) {
  AccessIndexResult out;

  if (source.size() < 2) {
    out.error = "stream too short for a zlib header";
    return out;
  }
  std::byte header[2] = {};
  source.read(0, header, 2);
  out.zlib_header = {header[0], header[1]};

  z_stream strm{};
  if (inflateInit(&strm) != Z_OK) {
    out.error = "inflateInit failed";
    return out;
  }

  std::vector<std::byte> in_buf(kInputChunk);
  std::vector<std::byte> scratch(kScratchSize);
  std::vector<std::byte> window;  // last 32 KiB of output

  std::uint64_t logical_offset = 0;
  bool input_eof = false;
  bool saw_first = false;
  std::uint64_t output_total = 0;
  std::uint64_t last_point_output = 0;
  bool have_point = false;

  auto refill = [&]() -> bool {
    if (strm.avail_in != 0) {
      return true;
    }
    if (input_eof) {
      return false;
    }
    const std::uint64_t remaining =
        source.size() > logical_offset ? source.size() - logical_offset : 0;
    if (remaining == 0) {
      input_eof = true;
      return false;
    }
    const std::size_t want = static_cast<std::size_t>(
        std::min<std::uint64_t>(remaining, in_buf.size()));
    if (!source.read(logical_offset, in_buf.data(), want)) {
      out.error = "reading the logical stream failed";
      return false;
    }
    logical_offset += want;
    if (logical_offset >= source.size()) {
      input_eof = true;
    }
    strm.next_in = reinterpret_cast<Bytef*>(in_buf.data());
    strm.avail_in = static_cast<uInt>(want);
    return true;
  };

  bool done = false;
  while (!done) {
    if (!refill()) {
      break;
    }
    strm.next_out = reinterpret_cast<Bytef*>(scratch.data());
    strm.avail_out = static_cast<uInt>(scratch.size());
    const int ret = inflate(&strm, Z_BLOCK);
    const std::size_t produced = scratch.size() - strm.avail_out;
    output_total += produced;
    if (produced != 0) {
      window.insert(window.end(), scratch.begin(),
                    scratch.begin() + static_cast<std::ptrdiff_t>(produced));
      if (window.size() > kWindowSize) {
        window.erase(window.begin(),
                     window.begin() +
                         static_cast<std::ptrdiff_t>(window.size() - kWindowSize));
      }
    }
    if (output_total > max_output_bytes) {
      out.error = "inflate output cap exceeded";
      inflateEnd(&strm);
      return out;
    }

    const bool at_boundary = ret == Z_OK && (strm.data_type & 128) != 0;
    if (at_boundary || ret == Z_STREAM_END) {
      const std::uint64_t unused =
          static_cast<std::uint64_t>(strm.data_type) & 0x1Fu;
      const std::uint64_t boundary_bit =
          static_cast<std::uint64_t>(strm.total_in) * 8 - unused;

      if (!saw_first) {
        // The zlib header boundary is the mandatory point at output offset 0.
        AccessPoint point;
        point.input_byte = boundary_bit / 8;
        point.prime_bits = static_cast<std::uint8_t>(boundary_bit % 8);
        point.prime_value = 0;
        point.output_offset = 0;
        out.points.push_back(std::move(point));
        saw_first = true;
        have_point = true;
        last_point_output = 0;
      } else if (at_boundary &&
                 output_total - last_point_output >= min_interval) {
        AccessPoint point;
        point.input_byte = boundary_bit / 8;
        point.prime_bits = static_cast<std::uint8_t>(boundary_bit % 8);
        if (point.prime_bits != 0 && point.input_byte < source.size()) {
          std::byte b{0};
          source.read(point.input_byte, &b, 1);
          point.prime_value = static_cast<std::uint8_t>(
              static_cast<unsigned>(b) &
              ((1u << point.prime_bits) - 1u));
        }
        point.output_offset = output_total;
        point.dictionary = window;
        out.points.push_back(std::move(point));
        have_point = true;
        last_point_output = output_total;
      }

      if (ret == Z_STREAM_END) {
        out.total_output_bytes = output_total;
        out.adler_ok = true;
        done = true;
      }
    } else if (ret == Z_OK) {
      continue;
    } else {
      if (ret == Z_DATA_ERROR) {
        out.error = "inflate data error (corrupt stream or bad Adler-32)";
        out.adler_ok = false;
      } else if (ret == Z_NEED_DICT) {
        out.error = "inflate needs a preset dictionary";
      } else if (ret == Z_BUF_ERROR) {
        out.error = "inflate stalled without progress";
      } else {
        out.error = "inflate failed";
      }
      inflateEnd(&strm);
      return out;
    }
  }
  inflateEnd(&strm);

  if (!done) {
    if (input_eof && out.error.empty()) {
      out.error = "truncated zlib stream (no end marker)";
    }
    return out;
  }
  out.success = true;
  return out;
}

ExtractResult extract_output(const AccessIndexResult& index,
                             const pnga::io::IByteSource& source,
                             std::uint64_t output_offset,
                             std::uint64_t length) {
  ExtractResult out;
  if (!index.success || index.points.empty()) {
    out.error = "no access index";
    return out;
  }
  if (output_offset + length > index.total_output_bytes ||
      output_offset + length < output_offset) {
    out.error = "output range out of bounds";
    return out;
  }
  // Source fingerprint: unchanged zlib header and an in-range source.
  if (source.size() < 2) {
    out.error = "source too short for the indexed stream";
    return out;
  }
  std::byte header[2] = {};
  source.read(0, header, 2);
  if (header[0] != index.zlib_header[0] || header[1] != index.zlib_header[1]) {
    out.error = "source changed since indexing";
    return out;
  }

  // Nearest access point at or before the target output offset.
  std::size_t lo = 0;
  std::size_t hi = index.points.size();
  while (lo + 1 < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (index.points[mid].output_offset <= output_offset) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  const AccessPoint& point = index.points[lo];
  if (point.input_byte >= source.size()) {
    out.error = "source too short for the access point";
    return out;
  }
  if (point.prime_bits != 0) {
    std::byte b{0};
    source.read(point.input_byte, &b, 1);
    const std::uint8_t low =
        static_cast<std::uint8_t>(static_cast<unsigned>(b) &
                                  ((1u << point.prime_bits) - 1u));
    if (low != point.prime_value) {
      out.error = "source changed at the access point";
      return out;
    }
  }

  z_stream strm{};
  if (inflateInit2(&strm, -15) != Z_OK) {  // raw inflate: no zlib wrapper
    out.error = "inflateInit2 failed";
    return out;
  }
  inflateReset(&strm);
  if (point.output_offset > 0) {
    if (point.dictionary.empty() || point.dictionary.size() > kWindowSize ||
        inflateSetDictionary(&strm, reinterpret_cast<Bytef*>(
                                         const_cast<std::byte*>(point.dictionary.data())),
                             static_cast<uInt>(point.dictionary.size())) != Z_OK) {
      inflateEnd(&strm);
      out.error = "dictionary restore failed";
      return out;
    }
  }
  if (point.prime_bits != 0 &&
      inflatePrime(&strm, point.prime_bits, point.prime_value) != Z_OK) {
    inflateEnd(&strm);
    out.error = "bit priming failed";
    return out;
  }

  std::vector<std::byte> in_buf(kInputChunk);
  std::vector<std::byte> scratch(kScratchSize);
  std::uint64_t logical_offset = point.input_byte;
  bool input_eof = false;
  std::uint64_t skip = output_offset - point.output_offset;
  out.data.resize(static_cast<std::size_t>(length));
  std::size_t out_pos = 0;
  bool done = false;
  bool ok = true;

  while (out_pos < length && !done) {
    if (strm.avail_in == 0) {
      if (input_eof) {
        out.error = "input exhausted before the requested output";
        ok = false;
        break;
      }
      const std::uint64_t remaining =
          source.size() > logical_offset ? source.size() - logical_offset : 0;
      if (remaining == 0) {
        input_eof = true;
        out.error = "input exhausted before the requested output";
        ok = false;
        break;
      }
      const std::size_t want = static_cast<std::size_t>(
          std::min<std::uint64_t>(remaining, in_buf.size()));
      if (!source.read(logical_offset, in_buf.data(), want)) {
        out.error = "reading the logical stream failed";
        ok = false;
        break;
      }
      logical_offset += want;
      if (logical_offset >= source.size()) {
        input_eof = true;
      }
      strm.next_in = reinterpret_cast<Bytef*>(in_buf.data());
      strm.avail_in = static_cast<uInt>(want);
    }
    strm.next_out = reinterpret_cast<Bytef*>(scratch.data());
    strm.avail_out = static_cast<uInt>(scratch.size());
    const int ret = inflate(&strm, Z_NO_FLUSH);
    const std::size_t produced = scratch.size() - strm.avail_out;
    if (ret == Z_STREAM_END) {
      done = true;
    } else if (ret != Z_OK) {
      out.error = "inflate failed during extraction";
      ok = false;
      break;
    }
    std::size_t i = 0;
    if (skip > 0) {
      const std::size_t take =
          static_cast<std::size_t>(std::min<std::uint64_t>(skip, produced));
      skip -= take;
      i += take;
    }
    const std::size_t take =
        std::min<std::size_t>(static_cast<std::size_t>(length) - out_pos,
                              produced - i);
    std::memcpy(out.data.data() + out_pos, scratch.data() + i, take);
    out_pos += take;
  }
  inflateEnd(&strm);

  if (!ok) {
    out.data.clear();
    return out;
  }
  if (out_pos != static_cast<std::size_t>(length)) {
    out.error = "requested output not fully produced";
    out.data.clear();
    return out;
  }
  out.success = true;
  return out;
}

}  // namespace pnga::deflate_index
