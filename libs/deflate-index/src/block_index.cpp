// WP-401 block index implementation. One sequential inflate(Z_BLOCK) pass over
// the logical zlib stream records block boundaries; block type and BFINAL are
// read back from the 3-bit block header at each recorded boundary. Structured
// wrapper, Adler-32 and stop facts are extracted during the same pass with
// public zlib APIs only (no second inflate pass, no private zlib state).

#include "pnga/deflate-index/block_index.h"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <vector>

namespace pnga::deflate_index {

namespace {

constexpr std::size_t kInputChunk = 1 << 16;
constexpr std::size_t kScratchSize = 1 << 16;
// First planned block capacity; growth doubles from here under the retained
// bytes budget, so small streams need a single allocation.
constexpr std::size_t kInitialBlockCapacity = 16;

const char* type_text(BlockType type) noexcept {
  switch (type) {
    case BlockType::kStored:
      return "stored";
    case BlockType::kFixed:
      return "fixed";
    case BlockType::kDynamic:
      return "dynamic";
  }
  return "invalid";
}

// Reads the two zlib header bytes with public byte-source reads and records
// the structured RFC 1950 facts. An unreadable or absent header keeps the
// default (invalid) wrapper; the inflate pass reports the failure.
ZlibWrapperInfo read_wrapper(const pnga::io::IByteSource& source) {
  ZlibWrapperInfo wrapper;
  if (source.size() < 2) {
    return wrapper;
  }
  std::byte header[2] = {};
  if (!source.read(0, header, 2)) {
    return wrapper;
  }
  wrapper.cmf = static_cast<std::uint8_t>(header[0]);
  wrapper.flg = static_cast<std::uint8_t>(header[1]);
  wrapper.compression_method = static_cast<std::uint8_t>(wrapper.cmf & 0x0Fu);
  const unsigned cinfo = (wrapper.cmf >> 4) & 0x0Fu;
  wrapper.window_bits = static_cast<std::uint8_t>(cinfo + 8);
  wrapper.preset_dictionary = (wrapper.flg & 0x20u) != 0;
  const unsigned fcheck =
      ((static_cast<unsigned>(wrapper.cmf) << 8) | wrapper.flg) % 31u;
  wrapper.header_valid = wrapper.compression_method == 8 && cinfo <= 7 &&
                         fcheck == 0;
  return wrapper;
}

// Reads the four-byte big-endian expected Adler-32 trailer at `offset`.
bool read_expected_adler(const pnga::io::IByteSource& source,
                         std::uint64_t offset, std::uint32_t& expected) {
  if (offset > std::numeric_limits<std::uint64_t>::max() - 4) {
    return false;
  }
  std::byte trailer[4] = {};
  if (!source.read(offset, trailer, 4)) {
    return false;
  }
  expected = (static_cast<std::uint32_t>(trailer[0]) << 24) |
             (static_cast<std::uint32_t>(trailer[1]) << 16) |
             (static_cast<std::uint32_t>(trailer[2]) << 8) |
             static_cast<std::uint32_t>(trailer[3]);
  return true;
}

// Records the latest verified boundary as the stop fact of a failed scan.
void assign_stop(BlockIndexResult& result, bool have_prev,
                 std::uint64_t prev_bit, std::uint64_t prev_output) noexcept {
  result.stop_input_bit = have_prev ? prev_bit : 0;
  result.stop_output_byte = have_prev ? prev_output : 0;
}

// Reads `count` (<= 16) bits at bit offset `bit_pos` from the logical stream in
// Deflate bit order (LSB-first within each byte, RFC 1951 §3.1.1). Returns
// false when the bits are not fully within the source.
bool read_bits(const pnga::io::IByteSource& source, std::uint64_t bit_pos,
               unsigned count, std::uint16_t& value) {
  if (count > 16) {
    return false;
  }
  const std::uint64_t byte_pos = bit_pos / 8;
  // Up to (bit_pos % 8) + count bits span at most 3 bytes.
  std::byte bytes[3] = {};
  const std::uint64_t byte_count =
      std::min<std::uint64_t>(3, source.size() > byte_pos ? source.size() - byte_pos : 0);
  if (byte_count < 3) {
    // Zero-fill past the end; the caller checks the range explicitly.
    std::memset(bytes, 0, sizeof(bytes));
  }
  if (byte_count != 0 &&
      !source.read(byte_pos, bytes, static_cast<std::size_t>(byte_count))) {
    return false;
  }
  // `bytes` is the window starting at bit_pos, so bit (bit_pos + k) lives at
  // the relative window byte selected by (bit_offset + k) / 8. The stream bit
  // at position k becomes value bit k, so the first bit read (BFINAL) lands in
  // the LSB of `value`.
  value = 0;
  const unsigned bit_offset = static_cast<unsigned>(bit_pos % 8);
  for (unsigned k = 0; k < count; ++k) {
    const unsigned relative_bit = bit_offset + k;
    const unsigned shift = relative_bit % 8;
    const std::uint16_t bit =
        (static_cast<unsigned>(bytes[relative_bit / 8]) >> shift) & 1u;
    value = static_cast<std::uint16_t>(value | (bit << k));
  }
  return true;
}

// Multiplies a block count by the record size with an overflow check.
bool checked_block_bytes(std::uint64_t blocks, std::uint64_t* output) noexcept {
  const std::uint64_t size = sizeof(DeflateBlock);
  if (blocks > std::numeric_limits<std::uint64_t>::max() / size) {
    return false;
  }
  *output = blocks * size;
  return true;
}

// The shared single-pass scan behind index_blocks and index_blocks_bounded.
// One sequential inflate(Z_BLOCK) pass records block boundaries; the bounded
// entry adds checked limits (trimmed input reads, planned block-vector
// growth with peak accounting) and cooperative cancellation. Both entries
// share this implementation — there is no second Inflate algorithm.
BoundedBlockIndexResult scan_blocks(const pnga::io::IByteSource& source,
                                    const BlockScanLimits& limits,
                                    const std::function<bool()>& cancelled) {
  BoundedBlockIndexResult out;
  out.stop = BlockScanStop::kInvalidInput;
  BlockIndexResult& result = out.index;

  result.wrapper = read_wrapper(source);
  if (result.wrapper.header_valid && result.wrapper.preset_dictionary) {
    // FDICT: the DEFLATE payload starts after 2 header + 4 DICTID bytes,
    // independent of whether a dictionary is available to decode it.
    result.zlib_header_bits = 48;
  }

  const auto cancelled_now = [&cancelled]() -> bool {
    return static_cast<bool>(cancelled) && cancelled();
  };

  z_stream strm{};
  if (inflateInit(&strm) != Z_OK) {
    result.error = "inflateInit failed";
    assign_stop(result, false, 0, 0);
    return out;
  }

  std::vector<std::byte> in_buf(kInputChunk);
  std::vector<std::byte> scratch(kScratchSize);

  std::uint64_t logical_offset = 0;  // next byte to read from `source`
  bool input_eof = false;
  bool saw_first = false;      // false until the zlib header boundary is seen
  bool have_prev = false;      // a block-start boundary is pending
  std::uint64_t prev_bit = 0;  // input bit where the current block starts
  std::uint64_t prev_output = 0;
  std::uint64_t output_total = 0;
  std::uint32_t actual_adler = adler32(0L, Z_NULL, 0);
  bool deflate_complete = false;  // a BFINAL block was fully verified

  // Stops the scan for a cooperative cancellation. The verified prefix and
  // the latest verified boundary stay assigned.
  const auto stop_cancelled = [&]() {
    result.error = "block scan cancelled";
    assign_stop(result, have_prev, prev_bit, prev_output);
    out.stop = BlockScanStop::kCancelled;
  };
  // Stops the scan for an exhausted work limit.
  const auto stop_budget = [&](const char* message) {
    result.error = message;
    assign_stop(result, have_prev, prev_bit, prev_output);
    out.stop = BlockScanStop::kBudgetExceeded;
  };

  // Appends one verified block under the count and retained-bytes budgets.
  // Capacity growth is planned: the old and the growing buffer coexist
  // during the move, so the peak (old + new capacity) must fit the budget
  // before the reservation happens. Returns false when a limit stopped the
  // scan (the stop reason and coordinates are already assigned).
  const auto append_block = [&](const DeflateBlock& block) -> bool {
    if (limits.max_blocks != 0 &&
        result.blocks.size() >= limits.max_blocks) {
      stop_budget("block scan block budget exceeded");
      return false;
    }
    if (result.blocks.size() == result.blocks.capacity()) {
      const std::uint64_t old_capacity = result.blocks.capacity();
      const std::uint64_t new_capacity =
          old_capacity == 0 ? kInitialBlockCapacity : old_capacity * 2;
      if (limits.max_retained_bytes != 0) {
        std::uint64_t peak_bytes = 0;
        if (!checked_block_bytes(old_capacity + new_capacity, &peak_bytes) ||
            peak_bytes > limits.max_retained_bytes) {
          stop_budget("block scan retained bytes budget exceeded");
          return false;
        }
      }
      result.blocks.reserve(static_cast<std::size_t>(new_capacity));
      // An allocator may hand out more than requested; re-check so the
      // recorded reality never exceeds the budget.
      if (limits.max_retained_bytes != 0) {
        std::uint64_t actual_bytes = 0;
        if (!checked_block_bytes(result.blocks.capacity(), &actual_bytes) ||
            actual_bytes > limits.max_retained_bytes) {
          stop_budget("block scan retained bytes budget exceeded");
          return false;
        }
      }
    }
    result.blocks.push_back(block);
    return true;
  };

  // Refills zlib's input from the logical stream. kStopped reports a
  // budget stop, a cancellation or a read failure (the stop reason and
  // coordinates are already assigned); kEof reports a genuine end of
  // input.
  enum class RefillResult { kOk, kEof, kStopped };
  const auto refill = [&]() -> RefillResult {
    if (strm.avail_in != 0) {
      return RefillResult::kOk;
    }
    if (cancelled_now()) {
      stop_cancelled();
      return RefillResult::kStopped;
    }
    if (input_eof) {
      return RefillResult::kEof;
    }
    std::uint64_t remaining =
        source.size() > logical_offset ? source.size() - logical_offset : 0;
    if (limits.max_input_bytes != 0) {
      // Trim the read length to the input budget before touching the
      // source; consuming past the limit is never allowed.
      remaining = std::min(remaining, logical_offset >= limits.max_input_bytes
                                          ? 0
                                          : limits.max_input_bytes -
                                                logical_offset);
    }
    if (remaining == 0) {
      if (logical_offset < source.size() && limits.max_input_bytes != 0) {
        stop_budget("block scan input budget exceeded");
        return RefillResult::kStopped;
      }
      input_eof = true;
      return RefillResult::kEof;
    }
    const std::size_t want = static_cast<std::size_t>(
        std::min<std::uint64_t>(remaining, in_buf.size()));
    if (!source.read(logical_offset, in_buf.data(), want)) {
      result.error = "reading the logical stream failed";
      assign_stop(result, have_prev, prev_bit, prev_output);
      return RefillResult::kStopped;
    }
    logical_offset += want;
    if (logical_offset >= source.size()) {
      input_eof = true;
    }
    strm.next_in = reinterpret_cast<Bytef*>(in_buf.data());
    strm.avail_in = static_cast<uInt>(want);
    return RefillResult::kOk;
  };

  bool done = false;
  while (!done) {
    if (cancelled_now()) {
      stop_cancelled();
      inflateEnd(&strm);
      return out;
    }
    const RefillResult refilled = refill();
    if (refilled == RefillResult::kStopped) {
      inflateEnd(&strm);
      return out;
    }
    if (refilled == RefillResult::kEof) {
      break;  // input exhausted before Z_STREAM_END (truncated or read error)
    }
    strm.next_out = reinterpret_cast<Bytef*>(scratch.data());
    strm.avail_out = static_cast<uInt>(scratch.size());
    const int ret = inflate(&strm, Z_BLOCK);
    const std::size_t produced = scratch.size() - strm.avail_out;
    output_total += produced;
    if (produced != 0) {
      actual_adler = adler32(actual_adler,
                             reinterpret_cast<const Bytef*>(scratch.data()),
                             static_cast<uInt>(produced));
    }
    if (limits.max_output_bytes != 0 && output_total > limits.max_output_bytes) {
      stop_budget("inflate output cap exceeded");
      inflateEnd(&strm);
      return out;
    }

    // Z_BLOCK is a flush option, not a return code: inflate returns Z_OK at
    // every block boundary (and right after the zlib header), with mode == TYPE
    // reported as data_type bit 7. The first such return is the header; every
    // later one completes a block. Z_STREAM_END only confirms the trailing
    // Adler-32 and never starts a block.
    const bool at_boundary =
        ret == Z_OK && (strm.data_type & 128) != 0;
    if (at_boundary || ret == Z_STREAM_END) {
      const std::uint64_t unused =
          static_cast<std::uint64_t>(strm.data_type) & 0x1Fu;
      const std::uint64_t boundary_bit =
          static_cast<std::uint64_t>(strm.total_in) * 8 - unused;

      if (!saw_first) {
        // The zlib header boundary, just before block 0.
        result.zlib_header_bits = boundary_bit;
        prev_bit = boundary_bit;
        prev_output = output_total;
        have_prev = true;
        saw_first = true;
      } else if (at_boundary) {
        // A block just completed: [prev_bit, boundary_bit) input bits produced
        // output [prev_output, output_total).
        if (have_prev) {
          DeflateBlock block;
          block.index = result.blocks.size();
          block.input_bit_begin = prev_bit;
          block.input_bit_end = boundary_bit;
          block.output_begin = prev_output;
          block.output_end = output_total;
          std::uint16_t header = 0;
          if (!read_bits(source, prev_bit, 3, header) ||
              prev_bit + 3 > source.size() * 8) {
            result.error = "block header out of range";
            assign_stop(result, have_prev, prev_bit, prev_output);
            inflateEnd(&strm);
            return out;
          }
          block.last = (header & 0x1) != 0;
          const std::uint8_t type_bits = static_cast<std::uint8_t>((header >> 1) & 0x3);
          if (type_bits == 3) {
            result.error = "reserved deflate block type";
            assign_stop(result, have_prev, prev_bit, prev_output);
            inflateEnd(&strm);
            return out;
          }
          block.type = static_cast<BlockType>(type_bits);
          deflate_complete = block.last;
          if (!append_block(block)) {
            inflateEnd(&strm);
            return out;
          }
        }
        prev_bit = boundary_bit;
        prev_output = output_total;
        have_prev = true;
      }

      if (ret == Z_STREAM_END) {
        result.total_output_bytes = output_total;
        result.adler.actual = actual_adler;
        std::uint32_t expected = 0;
        const std::uint64_t total_in =
            static_cast<std::uint64_t>(strm.total_in);
        if (total_in >= 4 &&
            read_expected_adler(source, total_in - 4, expected)) {
          result.adler.expected = expected;
          result.adler.status = expected == actual_adler
                                  ? Adler32Status::kMatch
                                  : Adler32Status::kMismatch;
        }
        done = true;
      }
    } else if (ret == Z_OK) {
      continue;  // mid-block; refill or flush more output
    } else {
      if (ret == Z_DATA_ERROR) {
        const bool invalid_block_type =
            strm.msg != nullptr &&
            std::strcmp(strm.msg, "invalid block type") == 0;
        if (invalid_block_type) {
          // zlib rejected the 3-bit block header itself; decoding stopped
          // right after those three bits.
          result.error = "reserved deflate block type";
          result.stop_input_bit =
              have_prev && prev_bit <=
                               std::numeric_limits<std::uint64_t>::max() - 3
                  ? prev_bit + 3
                  : std::numeric_limits<std::uint64_t>::max();
          result.stop_output_byte = have_prev ? prev_output : 0;
        } else {
          result.error = "inflate data error (corrupt stream or bad Adler-32)";
          assign_stop(result, have_prev, prev_bit, prev_output);
          if (deflate_complete) {
            // Only the trailing Adler-32 remains after the BFINAL block, so
            // this data error is the checksum comparison failing.
            std::uint32_t expected = 0;
            const std::uint64_t total_in =
                static_cast<std::uint64_t>(strm.total_in);
            if (total_in >= 4 &&
                read_expected_adler(source, total_in - 4, expected)) {
              result.adler.status = Adler32Status::kMismatch;
              result.adler.expected = expected;
              result.adler.actual = actual_adler;
            }
          }
        }
      } else if (ret == Z_NEED_DICT) {
        result.error = "inflate needs a preset dictionary";
        assign_stop(result, have_prev, prev_bit, prev_output);
      } else if (ret == Z_BUF_ERROR) {
        result.error = "inflate stalled without progress";
        assign_stop(result, have_prev, prev_bit, prev_output);
      } else {
        result.error = "inflate failed";
        assign_stop(result, have_prev, prev_bit, prev_output);
      }
      inflateEnd(&strm);
      return out;
    }
  }
  inflateEnd(&strm);

  if (!done) {
    if (input_eof && result.error.empty()) {
      result.error = "truncated zlib stream (no end marker)";
    }
    assign_stop(result, have_prev, prev_bit, prev_output);
    return out;
  }
  result.success = true;
  out.stop = BlockScanStop::kComplete;
  return out;
}

}  // namespace

const char* block_type_text(BlockType type) noexcept { return type_text(type); }

BlockIndexResult index_blocks(const pnga::io::IByteSource& source,
                              std::uint64_t max_output_bytes) {
  BlockScanLimits limits;
  limits.max_output_bytes = max_output_bytes;
  // The frozen entry keeps its original semantics: only the output cap is
  // enforced, no cancellation, identical error messages.
  return scan_blocks(source, limits, {}).index;
}

BoundedBlockIndexResult index_blocks_bounded(
    const pnga::io::IByteSource& source, const BlockScanLimits& limits,
    const std::function<bool()>& cancelled) {
  return scan_blocks(source, limits, cancelled);
}

std::optional<std::size_t> block_for_output(const BlockIndexResult& index,
                                            std::uint64_t output_offset) {
  for (std::size_t i = 0; i < index.blocks.size(); ++i) {
    const auto& b = index.blocks[i];
    if (output_offset >= b.output_begin && output_offset < b.output_end) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace pnga::deflate_index
