// WP-401 block index tests: stored/fixed/dynamic blocks, multiple blocks,
// header/Adler across IDAT boundaries, mid-byte boundaries, output-range
// tiling, error handling and block_for_output lookup.

#include <pnga/deflate-index/block_index.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/png-format/virtual_idat_stream.h>

#include <catch2/catch_test_macros.hpp>

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using pnga::deflate_index::block_for_output;
using pnga::deflate_index::Adler32Status;
using pnga::deflate_index::BlockIndexResult;
using pnga::deflate_index::BlockType;
using pnga::deflate_index::index_blocks;
using pnga::io::IByteSource;
using pnga::io::MemoryByteSource;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> make_compressible(std::size_t n) {
  std::vector<std::byte> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = B(static_cast<unsigned char>((i % 31) + 1));
  }
  return out;
}

// zlib-compresses `raw` with an explicit deflate level/strategy so the block
// types are controlled: level 0 -> stored, Z_FIXED -> fixed, default on
// compressible input -> dynamic.
std::vector<std::byte> zlib_compress(const std::vector<std::byte>& raw,
                                     int level, int strategy) {
  z_stream strm{};
  // windowBits 15 wraps the deflate stream in a zlib header + Adler-32.
  if (deflateInit2(&strm, level, Z_DEFLATED, 15, 8, strategy) != Z_OK) {
    return {};
  }
  const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> out(static_cast<std::size_t>(bound));
  strm.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(raw.data()));
  strm.avail_in = static_cast<uInt>(raw.size());
  strm.next_out = reinterpret_cast<Bytef*>(out.data());
  strm.avail_out = static_cast<uInt>(out.size());
  const int rc = deflate(&strm, Z_FINISH);
  if (rc != Z_STREAM_END) {
    deflateEnd(&strm);
    return {};
  }
  out.resize(strm.total_out);
  deflateEnd(&strm);
  return out;
}

// Verifies the index tiles the stream: header, then contiguous blocks.
void require_tiling(const BlockIndexResult& index, std::uint64_t raw_size) {
  REQUIRE(index.success);
  REQUIRE_FALSE(index.blocks.empty());
  REQUIRE(index.zlib_header_bits == 16);  // 2-byte zlib header
  REQUIRE(index.wrapper.cmf == 0x78);
  REQUIRE(index.wrapper.compression_method == 8);
  REQUIRE(index.wrapper.window_bits == 15);
  REQUIRE(index.wrapper.header_valid);
  REQUIRE_FALSE(index.wrapper.preset_dictionary);
  REQUIRE(index.adler.status == Adler32Status::kMatch);
  REQUIRE(index.adler.expected.has_value());
  REQUIRE(index.adler.actual.has_value());
  REQUIRE(index.adler.expected == index.adler.actual);
  REQUIRE_FALSE(index.stop_input_bit.has_value());
  REQUIRE_FALSE(index.stop_output_byte.has_value());
  for (std::size_t i = 0; i < index.blocks.size(); ++i) {
    const auto& b = index.blocks[i];
    REQUIRE(b.index == i);
    REQUIRE(b.input_bit_begin ==
            (i == 0 ? index.zlib_header_bits : index.blocks[i - 1].input_bit_end));
    REQUIRE(b.output_begin == (i == 0 ? 0 : index.blocks[i - 1].output_end));
    REQUIRE(b.output_end >= b.output_begin);
    REQUIRE(b.input_bit_end >= b.input_bit_begin);
  }
  REQUIRE(index.blocks.back().output_end == index.total_output_bytes);
  REQUIRE(index.total_output_bytes == raw_size);
  REQUIRE(index.adler.status == Adler32Status::kMatch);
}

// Adapts a VirtualIDATStream to IByteSource (same pattern as the analysis
// engine): the block index must work over the logical stream.
class VirtualIdatSource final : public IByteSource {
 public:
  VirtualIdatSource(const pnga::png_format::VirtualIDATStream& stream,
                    const IByteSource& file)
      : stream_(stream), file_(file) {}

  std::uint64_t size() const noexcept override { return stream_.size(); }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    return stream_.read(file_, offset, out, length);
  }
  std::optional<pnga::io::ByteView> view(std::uint64_t,
                                         std::size_t) const noexcept override {
    return std::nullopt;
  }

 private:
  const pnga::png_format::VirtualIDATStream& stream_;
  const IByteSource& file_;
};

// Wraps `compressed` as two IDAT chunks (the zlib stream split mid-stream) and
// returns the file bytes; the caller parses them into source/index/stream.
std::vector<std::byte> build_two_idats(const std::vector<std::byte>& compressed,
                                       std::size_t split) {
  std::vector<std::byte> bytes;
  bytes.insert(bytes.end(), pnga::png_format::kPngSignature.begin(),
               pnga::png_format::kPngSignature.end());
  auto push = [&](const char* type, const std::vector<std::byte>& data) {
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    bytes.push_back(B(static_cast<unsigned char>(len >> 24)));
    bytes.push_back(B(static_cast<unsigned char>(len >> 16)));
    bytes.push_back(B(static_cast<unsigned char>(len >> 8)));
    bytes.push_back(B(static_cast<unsigned char>(len)));
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(B(static_cast<unsigned char>(type[i])));
    }
    bytes.insert(bytes.end(), data.begin(), data.end());
    bytes.insert(bytes.end(), 4, std::byte{0});  // CRC not validated here
  };
  push("IHDR", std::vector<std::byte>(13, std::byte{0}));
  std::vector<std::byte> c1(compressed.begin(), compressed.begin() + split);
  std::vector<std::byte> c2(compressed.begin() + split, compressed.end());
  push("IDAT", c1);
  push("IDAT", c2);
  push("IEND", {});
  return bytes;
}

}  // namespace

TEST_CASE("Stored-block stream is indexed with stored type and byte alignment",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(4096);
  const auto compressed = zlib_compress(raw, /*level=*/0, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(compressed.empty());

  MemoryByteSource source(compressed);
  const BlockIndexResult index = index_blocks(source, 1u << 20);
  require_tiling(index, raw.size());
  for (const auto& b : index.blocks) {
    REQUIRE(b.type == BlockType::kStored);
    REQUIRE(b.input_bit_begin % 8 == 0);  // stored blocks are byte-aligned
    REQUIRE(b.input_bit_end % 8 == 0);
  }
}

TEST_CASE("Fixed-huffman stream is indexed with fixed type",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(4096);
  const auto compressed = zlib_compress(raw, /*level=*/6, Z_FIXED);
  REQUIRE_FALSE(compressed.empty());

  MemoryByteSource source(compressed);
  const BlockIndexResult index = index_blocks(source, 1u << 20);
  require_tiling(index, raw.size());
  for (const auto& b : index.blocks) {
    REQUIRE(b.type == BlockType::kFixed);
  }
}

TEST_CASE("Dynamic-huffman stream is indexed with dynamic type and mid-byte bits",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(200 * 1024);
  const auto compressed = zlib_compress(raw, /*level=*/6, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(compressed.empty());

  MemoryByteSource source(compressed);
  const BlockIndexResult index = index_blocks(source, 1u << 20);
  require_tiling(index, raw.size());
  for (const auto& b : index.blocks) {
    REQUIRE(b.type == BlockType::kDynamic);
  }
  // Huffman codes are not byte aligned, so the last dynamic block ends
  // mid-byte (unlike stored blocks).
  REQUIRE(index.blocks.back().input_bit_end % 8 != 0);
}

TEST_CASE("Large stored input produces many blocks with contiguous output",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(300 * 1024);
  const auto compressed = zlib_compress(raw, /*level=*/0, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(compressed.empty());

  MemoryByteSource source(compressed);
  const BlockIndexResult index = index_blocks(source, 1u << 20);
  require_tiling(index, raw.size());
  REQUIRE(index.blocks.size() >= 4);  // stored blocks are <= 65535 bytes each
}

TEST_CASE("Zlib header and Adler across two IDAT segments index cleanly",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(100 * 1024);
  const auto compressed = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(compressed.empty());

  MemoryByteSource file(build_two_idats(compressed, compressed.size() / 2));
  const auto index_result = pnga::png_format::index_chunks(file);
  const pnga::png_format::VirtualIDATStream stream(index_result);
  REQUIRE(stream.segment_count() == 2);
  VirtualIdatSource logical(stream, file);
  const BlockIndexResult index = index_blocks(logical, 1u << 20);
  require_tiling(index, raw.size());

  // The first block's logical input range maps onto both physical IDATs.
  const auto& first = index.blocks.front();
  std::vector<pnga::png_format::PhysicalRange> spans;
  const std::uint64_t begin = first.input_bit_begin / 8;
  const std::uint64_t length = (first.input_bit_end + 7) / 8 - begin;
  REQUIRE(stream.logical_to_physical(begin, length, spans));
  REQUIRE(spans.size() >= 2);  // crosses the IDAT boundary
}

TEST_CASE("block_for_output locates the containing block",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(300 * 1024);
  const auto compressed = zlib_compress(raw, 0, Z_DEFAULT_STRATEGY);
  MemoryByteSource source(compressed);
  const BlockIndexResult index = index_blocks(source, 1u << 20);
  require_tiling(index, raw.size());

  REQUIRE_FALSE(block_for_output(index, raw.size()).has_value());  // one past
  for (std::size_t i = 0; i < index.blocks.size(); ++i) {
    const auto& b = index.blocks[i];
    REQUIRE(block_for_output(index, b.output_begin) == i);
    if (b.output_end > b.output_begin) {
      REQUIRE(block_for_output(index, b.output_end - 1) == i);
    }
  }
}

TEST_CASE("Bad Adler-32 is reported as a structured mismatch",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(1024);
  auto compressed = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(compressed.empty());
  // Corrupt the trailing Adler-32 bytes so only the checksum fails.
  compressed[compressed.size() - 2] ^= std::byte{0xFF};

  MemoryByteSource source(compressed);
  const BlockIndexResult index = index_blocks(source, 1u << 20);
  REQUIRE_FALSE(index.success);
  REQUIRE_FALSE(index.error.empty());
  REQUIRE(index.adler.status == Adler32Status::kMismatch);
  REQUIRE(index.adler.expected.has_value());
  REQUIRE(index.adler.actual.has_value());
  REQUIRE(index.adler.expected != index.adler.actual);
}

TEST_CASE("Truncated stream keeps verified blocks and exact stop facts",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(4096);
  auto compressed = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(compressed.empty());

  // Truncated: drop the trailing Adler bytes.
  auto truncated = compressed;
  truncated.resize(truncated.size() - 4);
  MemoryByteSource trunc_src(truncated);
  const BlockIndexResult truncated_result = index_blocks(trunc_src, 1u << 20);
  REQUIRE_FALSE(truncated_result.success);
  REQUIRE_FALSE(truncated_result.error.empty());
  REQUIRE(truncated_result.adler.status == Adler32Status::kNotComputed);
  REQUIRE_FALSE(truncated_result.adler.expected.has_value());
  REQUIRE_FALSE(truncated_result.adler.actual.has_value());
  // Every verified block survives with the exact verified boundary.
  REQUIRE_FALSE(truncated_result.blocks.empty());
  REQUIRE(truncated_result.stop_input_bit ==
          truncated_result.blocks.back().input_bit_end);
  REQUIRE(truncated_result.stop_output_byte ==
          truncated_result.blocks.back().output_end);
}

TEST_CASE("Reserved deflate block type fails with a stable stop bit",
          "[deflate-index][wp401]") {
  std::vector<std::byte> reserved;
  reserved.push_back(B(0x78));
  reserved.push_back(B(0x01));  // valid zlib header (FCHECK passes)
  reserved.push_back(B(0x06));  // BFINAL=0, BTYPE=11 (reserved)
  reserved.insert(reserved.end(), 4, std::byte{0});

  MemoryByteSource source(reserved);
  const BlockIndexResult index = index_blocks(source, 1u << 20);
  REQUIRE_FALSE(index.success);
  REQUIRE(index.error == "reserved deflate block type");
  REQUIRE(index.blocks.empty());
  REQUIRE(index.stop_input_bit == 19);  // 16 header bits + 3 block header bits
  REQUIRE(index.stop_output_byte == 0);
}

TEST_CASE("FDICT wrapper is exposed with a byte-aligned deflate origin",
          "[deflate-index][wp401]") {
  std::vector<std::byte> fdict;
  fdict.push_back(B(0x78));
  fdict.push_back(B(0x20));  // FDICT set; 0x7820 passes the FCHECK modulo
  fdict.push_back(B(0x12));  // DICTID bytes
  fdict.push_back(B(0x34));
  fdict.push_back(B(0x56));
  fdict.push_back(B(0x78));
  fdict.push_back(B(0x00));  // deflate payload start (never decoded)
  fdict.insert(fdict.end(), 4, std::byte{0});

  MemoryByteSource source(fdict);
  const BlockIndexResult index = index_blocks(source, 1u << 20);
  REQUIRE_FALSE(index.success);
  REQUIRE_FALSE(index.error.empty());
  REQUIRE(index.wrapper.cmf == 0x78);
  REQUIRE(index.wrapper.compression_method == 8);
  REQUIRE(index.wrapper.window_bits == 15);
  REQUIRE(index.wrapper.header_valid);
  REQUIRE(index.wrapper.preset_dictionary);
  REQUIRE(index.zlib_header_bits == 48);  // 2 header + 4 DICTID bytes
}

TEST_CASE("Truncated stream and output cap are rejected",
          "[deflate-index][wp401]") {
  const auto raw = make_compressible(4096);
  auto compressed = zlib_compress(raw, 6, Z_DEFAULT_STRATEGY);
  REQUIRE_FALSE(compressed.empty());

  // Truncated: drop the trailing Adler bytes.
  auto truncated = compressed;
  truncated.resize(truncated.size() - 4);
  MemoryByteSource trunc_src(truncated);
  const BlockIndexResult truncated_result = index_blocks(trunc_src, 1u << 20);
  REQUIRE_FALSE(truncated_result.success);
  REQUIRE_FALSE(truncated_result.error.empty());

  // Output cap below the real size.
  MemoryByteSource full_src(compressed);
  const BlockIndexResult capped = index_blocks(full_src, 100);
  REQUIRE_FALSE(capped.success);
  REQUIRE_FALSE(capped.error.empty());
}
