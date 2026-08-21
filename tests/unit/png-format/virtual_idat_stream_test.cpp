// WP-201 VirtualIDATStream tests: cross-segment reads, boundary-exact reads,
// logical<->physical mapping, zero-length IDATs and zero-copy construction.

#include <pnga/png-format/virtual_idat_stream.h>

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::ChunkNode;
using pnga::png_format::IdatSegment;
using pnga::png_format::index_chunks;
using pnga::png_format::kPngSignature;
using pnga::png_format::PhysicalRange;
using pnga::png_format::VirtualIDATStream;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> chunk_with_data(const char* type,
                                       std::vector<std::byte> data) {
  std::vector<std::byte> out;
  const auto len = static_cast<std::uint32_t>(data.size());
  out.push_back(B(static_cast<unsigned char>(len >> 24)));
  out.push_back(B(static_cast<unsigned char>(len >> 16)));
  out.push_back(B(static_cast<unsigned char>(len >> 8)));
  out.push_back(B(static_cast<unsigned char>(len)));
  for (int i = 0; i < 4; ++i) {
    out.push_back(B(static_cast<unsigned char>(type[i])));
  }
  out.insert(out.end(), data.begin(), data.end());
  out.insert(out.end(), 4, std::byte{0});  // CRC (not validated here)
  return out;
}

std::vector<std::byte> fill(unsigned char c, std::size_t n) {
  return std::vector<std::byte>(n, B(c));
}

// Returns an index over sig + IHDR + the given IDAT data payloads + IEND.
ChunkIndex index_with_idats(std::vector<std::vector<std::byte>> idat_data) {
  std::vector<std::byte> bytes;
  bytes.assign(kPngSignature.begin(), kPngSignature.end());
  auto ihdr = chunk_with_data("IHDR", fill(0x11, 13));
  bytes.insert(bytes.end(), ihdr.begin(), ihdr.end());
  for (auto& d : idat_data) {
    auto chunk = chunk_with_data("IDAT", d);
    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
  }
  auto iend = chunk_with_data("IEND", {});
  bytes.insert(bytes.end(), iend.begin(), iend.end());
  MemoryByteSource src(std::move(bytes));
  return index_chunks(src);
}

std::vector<std::byte> read_range(const VirtualIDATStream& stream,
                                  const pnga::io::IByteSource& src,
                                  std::uint64_t offset, std::size_t length,
                                  bool& ok) {
  std::vector<std::byte> out(length);
  ok = stream.read(src, offset, out.data(), length);
  return out;
}

}  // namespace

TEST_CASE("A single IDAT reads as a plain logical stream", "[png-format][wp201]") {
  const ChunkIndex index = index_with_idats({fill(0x41, 4)});
  const VirtualIDATStream stream(index);
  REQUIRE(stream.size() == 4);
  REQUIRE(stream.segment_count() == 1);
  REQUIRE(stream.segment(0).length == 4);

  MemoryByteSource src(std::vector<std::byte>(
      [&] {
        std::vector<std::byte> b;
        b.assign(kPngSignature.begin(), kPngSignature.end());
        auto c = chunk_with_data("IHDR", fill(0x11, 13));
        b.insert(b.end(), c.begin(), c.end());
        c = chunk_with_data("IDAT", fill(0x41, 4));
        b.insert(b.end(), c.begin(), c.end());
        c = chunk_with_data("IEND", {});
        b.insert(b.end(), c.begin(), c.end());
        return b;
      }()));

  bool ok = false;
  const auto data = read_range(stream, src, 0, 4, ok);
  REQUIRE(ok);
  REQUIRE(data == std::vector<std::byte>(4, B(0x41)));

  // Boundary-exact reads.
  ok = false;
  REQUIRE(read_range(stream, src, 0, 0, ok).empty());
  REQUIRE(ok);
  ok = false;
  (void)read_range(stream, src, 4, 1, ok);
  REQUIRE_FALSE(ok);  // one byte past logical EOF
  REQUIRE_FALSE(stream.read(src, 3, nullptr, 2));
  REQUIRE(stream.read(src, 4, nullptr, 0));  // empty read at logical EOF
}

TEST_CASE("Cross-segment reads concatenate without materializing a buffer",
          "[png-format][wp201]") {
  const ChunkIndex index =
      index_with_idats({fill(0x41, 4), fill(0x42, 5), fill(0x43, 2)});
  const VirtualIDATStream stream(index);
  REQUIRE(stream.size() == 11);
  REQUIRE(stream.segment_count() == 3);

  std::vector<std::byte> bytes;
  bytes.assign(kPngSignature.begin(), kPngSignature.end());
  for (auto c : {chunk_with_data("IHDR", fill(0x11, 13)),
                 chunk_with_data("IDAT", fill(0x41, 4)),
                 chunk_with_data("IDAT", fill(0x42, 5)),
                 chunk_with_data("IDAT", fill(0x43, 2)),
                 chunk_with_data("IEND", {})}) {
    bytes.insert(bytes.end(), c.begin(), c.end());
  }
  MemoryByteSource src(std::move(bytes));

  // Read spanning all three segments.
  bool ok = false;
  const auto all = read_range(stream, src, 0, 11, ok);
  REQUIRE(ok);
  REQUIRE(all == std::vector<std::byte>{B(0x41), B(0x41), B(0x41), B(0x41),
                                        B(0x42), B(0x42), B(0x42), B(0x42),
                                        B(0x42), B(0x43), B(0x43)});

  // Read starting exactly at a segment boundary and crossing two segments.
  ok = false;
  const auto cross = read_range(stream, src, 4, 7, ok);
  REQUIRE(ok);
  REQUIRE(cross == std::vector<std::byte>{B(0x42), B(0x42), B(0x42), B(0x42),
                                          B(0x42), B(0x43), B(0x43)});
}

TEST_CASE("One logical range maps to multiple physical ranges",
          "[png-format][wp201]") {
  const ChunkIndex index =
      index_with_idats({fill(0x41, 4), fill(0x42, 5)});
  const VirtualIDATStream stream(index);
  const auto& s0 = stream.segment(0);
  const auto& s1 = stream.segment(1);

  std::vector<PhysicalRange> spans;
  REQUIRE(stream.logical_to_physical(2, 5, spans));
  REQUIRE(spans.size() == 2);
  REQUIRE(spans[0] == PhysicalRange{s0.physical_offset + 2, 2});
  REQUIRE(spans[1] == PhysicalRange{s1.physical_offset, 3});
}

TEST_CASE("Physical offsets inside IDAT data map to logical positions",
          "[png-format][wp201]") {
  const ChunkIndex index =
      index_with_idats({fill(0x41, 4), fill(0x42, 5)});
  const VirtualIDATStream stream(index);
  const auto& s0 = stream.segment(0);
  const auto& s1 = stream.segment(1);

  REQUIRE(stream.physical_to_logical(s0.physical_offset) == 0);
  REQUIRE(stream.physical_to_logical(s0.physical_offset + 3) == 3);
  REQUIRE(stream.physical_to_logical(s1.physical_offset) == 4);
  REQUIRE(stream.physical_to_logical(s1.physical_offset + 4) == 8);

  // Header (1 byte before data), CRC (one past data) and unrelated bytes.
  REQUIRE_FALSE(stream.physical_to_logical(s0.physical_offset - 1).has_value());
  REQUIRE_FALSE(stream.physical_to_logical(s0.physical_offset + 4).has_value());
  REQUIRE_FALSE(stream.physical_to_logical(0).has_value());  // signature
}

TEST_CASE("Zero-length IDAT segments contribute no logical bytes",
          "[png-format][wp201]") {
  const ChunkIndex index =
      index_with_idats({fill(0x41, 2), {}, fill(0x42, 3)});
  const VirtualIDATStream stream(index);
  REQUIRE(stream.size() == 5);
  REQUIRE(stream.segment_count() == 3);
  REQUIRE(stream.segment(1).length == 0);
  REQUIRE(stream.segment(1).logical_start == 2);
  REQUIRE(stream.segment(2).logical_start == 2);
  REQUIRE(stream.segment(2).physical_offset > stream.segment(1).physical_offset);
}

TEST_CASE("Out-of-range logical requests fail without wrapping",
          "[png-format][wp201]") {
  const ChunkIndex index = index_with_idats({fill(0x41, 4)});
  const VirtualIDATStream stream(index);
  MemoryByteSource src(std::vector<std::byte>(20, std::byte{0}));

  std::vector<PhysicalRange> spans;
  REQUIRE_FALSE(stream.logical_to_physical(3, 2, spans));
  REQUIRE(spans.empty());
  REQUIRE_FALSE(stream.read(src, 3, nullptr, 2));
  REQUIRE_FALSE(stream.read(src, 0xFFFFFFFFFFFFFFFFull, nullptr, 1));
}

TEST_CASE("Building the stream never allocates the total IDAT payload",
          "[png-format][wp201]") {
  // A single huge IDAT: the segment table stays tiny even though the logical
  // stream is 100 MiB (ADR-0005). The ChunkIndex is fabricated directly
  // because a real index_chunks would reject a length whose data is absent.
  const std::uint64_t huge = 100ull * 1024 * 1024;

  ChunkIndex index;
  index.valid_signature = true;
  index.file_size = huge + 8 + 12 + 4;
  ChunkNode node;
  node.header_offset = 8;
  node.data_offset = 16;
  node.data_length = huge;
  node.crc_offset = 16 + huge;
  node.type = {std::byte{'I'}, std::byte{'D'}, std::byte{'A'},
               std::byte{'T'}};
  index.chunks.push_back(node);

  const VirtualIDATStream stream(index);
  REQUIRE(stream.size() == huge);
  REQUIRE(stream.segment_count() == 1);
  REQUIRE(stream.segment(0).length == huge);
  REQUIRE(stream.segment(0).logical_start == 0);
}
