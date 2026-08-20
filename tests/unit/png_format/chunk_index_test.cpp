// WP-101 Chunk Index tests: signature, envelope spans, zero-length/unknown
// chunks, truncated header/data/CRC, overflowing length, trailing bytes after
// IEND and zero-copy indexing of a synthetic >100 MiB chunk.

#include <pnga/png-format/chunk_index.h>
#include <pnga/io/byte_source.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using pnga::io::ByteView;
using pnga::io::IByteSource;
using pnga::io::MemoryByteSource;
using pnga::png_format::ChunkIndex;
using pnga::png_format::ChunkIssueKind;
using pnga::png_format::ChunkNode;
using pnga::png_format::index_chunks;

namespace {

std::byte B(unsigned char c) { return static_cast<std::byte>(c); }

std::vector<std::byte> chunk_bytes(const char* type, std::uint32_t length,
                                   std::uint32_t crc = 0) {
  std::vector<std::byte> out;
  out.push_back(B(static_cast<unsigned char>(length >> 24)));
  out.push_back(B(static_cast<unsigned char>(length >> 16)));
  out.push_back(B(static_cast<unsigned char>(length >> 8)));
  out.push_back(B(static_cast<unsigned char>(length)));
  for (int i = 0; i < 4; ++i) {
    out.push_back(B(static_cast<unsigned char>(type[i])));
  }
  for (std::uint32_t i = 0; i < length; ++i) {
    out.push_back(B(0x11));
  }
  out.push_back(B(static_cast<unsigned char>(crc >> 24)));
  out.push_back(B(static_cast<unsigned char>(crc >> 16)));
  out.push_back(B(static_cast<unsigned char>(crc >> 8)));
  out.push_back(B(static_cast<unsigned char>(crc)));
  return out;
}

std::vector<std::byte> png_bytes(std::vector<std::vector<std::byte>> chunks) {
  std::vector<std::byte> out;
  out.assign(pnga::png_format::kPngSignature.begin(),
             pnga::png_format::kPngSignature.end());
  for (auto& c : chunks) {
    out.insert(out.end(), c.begin(), c.end());
  }
  return out;
}

bool has_issue(const ChunkIndex& index, ChunkIssueKind kind) {
  for (const auto& issue : index.issues) {
    if (issue.kind == kind) {
      return true;
    }
  }
  return false;
}

// Test double: a >100 MiB single-chunk file whose body is never materialized.
// Proves index_chunks builds the index without allocating or copying the body.
class HugeChunkSource final : public IByteSource {
 public:
  explicit HugeChunkSource(std::uint32_t data_length)
      : backing_(pnga::png_format::kPngSignature.begin(),
                 pnga::png_format::kPngSignature.end()) {
    const std::uint64_t size = backing_.size() + 8 + data_length + 4;
    const auto len = static_cast<unsigned char>(data_length >> 24);
    backing_.push_back(B(len));
    backing_.push_back(B(static_cast<unsigned char>(data_length >> 16)));
    backing_.push_back(B(static_cast<unsigned char>(data_length >> 8)));
    backing_.push_back(B(static_cast<unsigned char>(data_length)));
    backing_.push_back(B('A'));
    backing_.push_back(B('B'));
    backing_.push_back(B('C'));
    backing_.push_back(B('D'));
    size_ = size;
  }

  std::uint64_t size() const noexcept override { return size_; }
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override {
    auto v = view(offset, length);
    if (!v.has_value()) {
      return false;
    }
    for (std::size_t i = 0; i < length; ++i) {
      out[i] = v->data[i];
    }
    return true;
  }
  std::optional<ByteView> view(std::uint64_t offset,
                               std::size_t length) const noexcept override {
    // Only the signature and the 8-byte header are materialized.
    if (offset + length <= backing_.size()) {
      return ByteView{backing_.data() + offset, length};
    }
    return std::nullopt;
  }

 private:
  std::vector<std::byte> backing_;
  std::uint64_t size_ = 0;
};

}  // namespace

TEST_CASE("Chunk index builds a valid minimal PNG", "[png-format][chunk-index]") {
  auto data = png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 8),
                         chunk_bytes("IEND", 0)});
  MemoryByteSource src(std::move(data));
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.valid_signature);
  REQUIRE(index.issues.empty());
  REQUIRE(index.chunks.size() == 3);

  const ChunkNode& ihdr = index.chunks[0];
  REQUIRE(ihdr.header_offset == 8);
  REQUIRE(ihdr.data_offset == 16);
  REQUIRE(ihdr.data_length == 13);
  REQUIRE(ihdr.crc_offset == 29);
  REQUIRE(ihdr.text() == "IHDR");

  const ChunkNode& idat = index.chunks[1];
  REQUIRE(idat.header_offset == 33);
  REQUIRE(idat.data_offset == 41);
  REQUIRE(idat.data_length == 8);
  REQUIRE(idat.crc_offset == 49);

  REQUIRE(index.chunks[2].text() == "IEND");
  REQUIRE(index.chunks[2].data_length == 0);
}

TEST_CASE("Chunk index handles a zero-length chunk", "[png-format][chunk-index]") {
  auto data = png_bytes({chunk_bytes("IDAT", 0), chunk_bytes("IEND", 0)});
  MemoryByteSource src(std::move(data));
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.valid_signature);
  REQUIRE(index.issues.empty());
  REQUIRE(index.chunks.size() == 2);
  REQUIRE(index.chunks[0].data_length == 0);
  REQUIRE(index.chunks[0].data_offset == index.chunks[0].crc_offset);
}

TEST_CASE("Chunk index accepts unknown and private chunk types",
          "[png-format][chunk-index]") {
  auto data = png_bytes({chunk_bytes("abcd", 4), chunk_bytes("IEND", 0)});
  MemoryByteSource src(std::move(data));
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.valid_signature);
  REQUIRE(index.issues.empty());
  REQUIRE(index.chunks.size() == 2);
  REQUIRE(index.chunks[0].text() == "abcd");
  REQUIRE(index.chunks[0].data_length == 4);
}

TEST_CASE("Chunk index reports a truncated signature", "[png-format][chunk-index]") {
  MemoryByteSource src(std::vector<std::byte>(4, std::byte{0}));
  const ChunkIndex index = index_chunks(src);

  REQUIRE_FALSE(index.valid_signature);
  REQUIRE(index.chunks.empty());
  REQUIRE(has_issue(index, ChunkIssueKind::kTruncatedSignature));
}

TEST_CASE("Chunk index rejects a bad signature", "[png-format][chunk-index]") {
  auto data = pnga::png_format::kPngSignature;
  data[0] = std::byte{0x00};  // not 0x89
  std::vector<std::byte> v(data.begin(), data.end());
  auto iend = chunk_bytes("IEND", 0);
  v.insert(v.end(), iend.begin(), iend.end());
  MemoryByteSource src(std::move(v));
  const ChunkIndex index = index_chunks(src);

  REQUIRE_FALSE(index.valid_signature);
  REQUIRE(index.chunks.empty());
  REQUIRE(has_issue(index, ChunkIssueKind::kBadSignature));
}

TEST_CASE("Chunk index reports a truncated header at EOF", "[png-format][chunk-index]") {
  auto data = png_bytes({});
  data.push_back(std::byte{0x00});  // signature + 1 stray byte
  MemoryByteSource src(std::move(data));
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.valid_signature);
  REQUIRE(index.chunks.empty());
  REQUIRE(has_issue(index, ChunkIssueKind::kTruncatedHeader));
}

TEST_CASE("Chunk index reports truncated data and preserves earlier nodes",
          "[png-format][chunk-index]") {
  auto ihdr = chunk_bytes("IHDR", 13);
  auto bad = chunk_bytes("IDAT", 100);  // declared 100, but we cut it short
  bad.resize(bad.size() - 10);          // drop 10 bytes of data+CRC
  MemoryByteSource src(png_bytes({std::move(ihdr), std::move(bad)}));
  const ChunkIndex index = index_chunks(src);

  // The valid IHDR is preserved; the malformed chunk is not.
  REQUIRE(index.chunks.size() == 1);
  REQUIRE(index.chunks[0].text() == "IHDR");
  REQUIRE(has_issue(index, ChunkIssueKind::kTruncatedData));
}

TEST_CASE("Chunk index reports a truncated CRC", "[png-format][chunk-index]") {
  auto chunk = chunk_bytes("IDAT", 4);
  chunk.pop_back();
  chunk.pop_back();  // only 2 of the 4 CRC bytes present
  MemoryByteSource src(png_bytes({std::move(chunk)}));
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.chunks.empty());
  REQUIRE(has_issue(index, ChunkIssueKind::kTruncatedCrc));
}

TEST_CASE("Chunk index rejects an overflowing length without wrapping",
          "[png-format][chunk-index]") {
  // Declared length 0xFFFFFFFF in a tiny file must fail cleanly. Build a small
  // chunk and patch its big-endian length field rather than allocating 4 GiB.
  auto chunk = chunk_bytes("IDAT", 8);
  chunk[0] = B(0xFF);
  chunk[1] = B(0xFF);
  chunk[2] = B(0xFF);
  chunk[3] = B(0xFF);
  auto data = png_bytes({std::move(chunk)});
  data.resize(8 + 8 + 4);  // signature + 8-byte header + 4 stray bytes
  MemoryByteSource src(std::move(data));
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.chunks.empty());
  REQUIRE(has_issue(index, ChunkIssueKind::kTruncatedData));
}

TEST_CASE("Chunk index reports trailing bytes after IEND", "[png-format][chunk-index]") {
  auto data = png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IEND", 0)});
  data.insert(data.end(), {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}});
  MemoryByteSource src(std::move(data));
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.chunks.size() == 2);
  REQUIRE(index.chunks.back().text() == "IEND");
  REQUIRE(has_issue(index, ChunkIssueKind::kTrailingBytesAfterIend));
}

TEST_CASE("Chunk index stops at IEND without scanning further",
          "[png-format][chunk-index]") {
  auto data = png_bytes({chunk_bytes("IEND", 0)});
  MemoryByteSource src(std::move(data));
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.chunks.size() == 1);
  REQUIRE(index.chunks[0].text() == "IEND");
  REQUIRE(index.issues.empty());  // no trailing bytes, clean stop
}

TEST_CASE("Chunk index builds a huge chunk index without copying its body",
          "[png-format][chunk-index]") {
  constexpr std::uint32_t kHugeLength = 100 * 1024 * 1024;  // 100 MiB
  HugeChunkSource src(kHugeLength);
  const ChunkIndex index = index_chunks(src);

  REQUIRE(index.valid_signature);
  REQUIRE(index.issues.empty());
  REQUIRE(index.chunks.size() == 1);
  REQUIRE(index.chunks[0].data_length == kHugeLength);
  // The index holds only envelope metadata, never the 100 MiB body.
  REQUIRE(index.chunks[0].data_offset == 16);
  REQUIRE(index.chunks[0].crc_offset == 16 + kHugeLength);
}

TEST_CASE("Chunk index is deterministic for identical input", "[png-format][chunk-index]") {
  auto bytes = png_bytes({chunk_bytes("IHDR", 13), chunk_bytes("IDAT", 0),
                          chunk_bytes("IEND", 0)});
  MemoryByteSource a(bytes);
  MemoryByteSource b(bytes);
  const ChunkIndex ia = index_chunks(a);
  const ChunkIndex ib = index_chunks(b);

  REQUIRE(ia.issues.empty());
  REQUIRE(ia.chunks.size() == ib.chunks.size());
  for (std::size_t i = 0; i < ia.chunks.size(); ++i) {
    REQUIRE(ia.chunks[i].header_offset == ib.chunks[i].header_offset);
    REQUIRE(ia.chunks[i].data_offset == ib.chunks[i].data_offset);
    REQUIRE(ia.chunks[i].data_length == ib.chunks[i].data_length);
    REQUIRE(ia.chunks[i].crc_offset == ib.chunks[i].crc_offset);
    REQUIRE(ia.chunks[i].text() == ib.chunks[i].text());
  }
}
