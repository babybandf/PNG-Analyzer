// WP-101 Chunk envelope index. The scanner walks Chunk headers with the
// invariant form  offset <= size - length  so no intermediate addition can
// overflow, even for a hostile 32-bit length field in a small file.

#include "pnga/png-format/chunk_index.h"

#include <algorithm>
#include <cstdint>

namespace pnga::png_format {

namespace {

constexpr std::uint64_t kHeaderSize = 8;  // 4-byte length + 4-byte type
constexpr std::uint64_t kCrcSize = 4;

std::uint32_t read_u32_be(const std::byte* p) noexcept {
  std::uint32_t v = 0;
  for (int i = 0; i < 4; ++i) {
    v = (v << 8) | static_cast<std::uint32_t>(p[i]);
  }
  return v;
}

bool is_end(const std::array<std::byte, 4>& type) noexcept {
  static constexpr std::array<std::byte, 4> kIend = {
      std::byte{'I'}, std::byte{'E'}, std::byte{'N'}, std::byte{'D'}};
  return type == kIend;
}

}  // namespace

std::string ChunkNode::text() const {
  std::string out;
  out.reserve(4);
  for (std::byte b : type) {
    out.push_back(static_cast<char>(b));
  }
  return out;
}

ChunkIndex index_chunks(const pnga::io::IByteSource& source) {
  ChunkIndex index;
  index.file_size = source.size();

  if (index.file_size < kPngSignature.size()) {
    index.issues.push_back(
        {ChunkIssueKind::kTruncatedSignature, index.file_size});
    return index;
  }

  auto sig = source.view(0, kPngSignature.size());
  if (!sig.has_value() ||
      !std::equal(kPngSignature.begin(), kPngSignature.end(), sig->data)) {
    index.issues.push_back({ChunkIssueKind::kBadSignature, 0});
    return index;
  }
  index.valid_signature = true;

  std::uint64_t pos = kPngSignature.size();
  while (true) {
    if (pos + kHeaderSize > index.file_size) {
      // Leftover bytes too short for a length+type header.
      if (pos < index.file_size) {
        index.issues.push_back({ChunkIssueKind::kTruncatedHeader, pos});
      }
      break;
    }

    auto header = source.view(pos, kHeaderSize);
    if (!header.has_value()) {
      index.issues.push_back({ChunkIssueKind::kTruncatedHeader, pos});
      break;
    }

    const std::uint64_t data_offset = pos + kHeaderSize;
    const std::uint64_t length = read_u32_be(header->data);

    // Checked: the declared data must fit before the source ends. Using the
    // invariant form avoids overflow for any 32-bit length.
    if (length > index.file_size - data_offset) {
      index.issues.push_back({ChunkIssueKind::kTruncatedData, data_offset});
      break;
    }

    const std::uint64_t crc_offset = data_offset + length;
    if (crc_offset + kCrcSize > index.file_size) {
      index.issues.push_back({ChunkIssueKind::kTruncatedCrc, crc_offset});
      break;
    }

    ChunkNode node;
    node.header_offset = pos;
    node.data_offset = data_offset;
    node.data_length = length;
    node.crc_offset = crc_offset;
    std::copy(header->data + 4, header->data + kHeaderSize, node.type.begin());
    index.chunks.push_back(node);

    if (is_end(node.type)) {
      const std::uint64_t end = crc_offset + kCrcSize;
      if (end < index.file_size) {
        index.issues.push_back(
            {ChunkIssueKind::kTrailingBytesAfterIend, end});
      }
      break;
    }

    pos = crc_offset + kCrcSize;
  }
  return index;
}

}  // namespace pnga::png_format
