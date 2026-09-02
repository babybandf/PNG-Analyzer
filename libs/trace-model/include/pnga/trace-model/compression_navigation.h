#ifndef PNGA_TRACE_MODEL_COMPRESSION_NAVIGATION_H
#define PNGA_TRACE_MODEL_COMPRESSION_NAVIGATION_H

// WP-5U12B: typed Compression navigation contract. One navigation carries a
// single typed logical range plus every mapped physical file span, so multi-
// IDAT values survive round trips without first-span-only shortcuts. The
// immutable Current mapping and the user-owned Manual Selection are separate
// value types and may coexist in one generation-scoped state.
//
// Physical file spans are validated in caller order and are never sorted,
// merged or truncated here.

#include <pnga/trace-model/offset_range.h>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace pnga::trace_model {

enum class DocumentSourceUnitKind { kFile = 0, kAnimationFrame = 1 };

struct DocumentSourceUnit {
  DocumentSourceUnitKind kind = DocumentSourceUnitKind::kFile;
  std::uint32_t index = 0;
  bool valid() const noexcept;  // File requires index 0.
  bool operator==(const DocumentSourceUnit&) const = default;
};

using CompressionLogicalRange = std::variant<
    FileByteRange, ZlibByteRange, ZlibBitRange,
    DeflateBitRange, InflatedByteRange>;

enum class CompressionNavigationOrigin {
  kBlocks = 0, kHuffman = 1, kDecodeTrace = 2,
  kHex = 3, kInflated = 4
};

struct CompressionNavigationTarget {
  std::uint64_t generation = 0;
  std::uint64_t request_serial = 0;
  DocumentSourceUnit source_unit{};
  CompressionNavigationOrigin origin = CompressionNavigationOrigin::kBlocks;
  CompressionLogicalRange logical_range = FileByteRange{};
  std::vector<FileByteRange> physical_spans;
  std::optional<std::uint64_t> block_index;
  std::optional<std::uint64_t> token_index;
  std::optional<std::uint16_t> symbol;
  bool valid() const noexcept;
  bool operator==(const CompressionNavigationTarget&) const = default;
};

struct CompressionCurrentMapping {
  std::uint64_t generation = 0;
  DocumentSourceUnit source_unit{};
  InflatedByteRange output_range{};
  std::optional<std::uint64_t> block_index;
  std::optional<std::uint64_t> token_index;
  bool operator==(const CompressionCurrentMapping&) const = default;
};

struct CompressionSelectionState {
  std::uint64_t generation = 0;
  std::optional<CompressionCurrentMapping> current;
  std::optional<CompressionNavigationTarget> manual;
  bool operator==(const CompressionSelectionState&) const = default;
};

}  // namespace pnga::trace_model

#endif  // PNGA_TRACE_MODEL_COMPRESSION_NAVIGATION_H
