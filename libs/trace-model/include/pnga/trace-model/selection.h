#ifndef PNGA_TRACE_MODEL_SELECTION_H
#define PNGA_TRACE_MODEL_SELECTION_H

// WP-200: the shared selection language (ADR-0004). A Selection may carry a
// semantic node id, one or more physical spans, a logical stream span, image
// coordinates and a decode stage. Equality, merge and deterministic
// serialization rules are defined here so GUI panels can synchronize without
// decoder-specific objects and without update loops.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pnga::trace_model {

// Stable identifier for an object within one document generation.
using NodeId = std::uint64_t;

// Physical span in the file. Byte spans use length in bytes; bit spans may set
// bit_aligned and a bit offset within the first byte.
struct BitSpan {
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
  std::uint8_t bit_offset = 0;  // meaningful only when bit_aligned
  bool bit_aligned = false;

  bool operator==(const BitSpan&) const = default;
};

// Logical span within a virtual stream (e.g. the concatenated IDAT payload).
struct StreamSpan {
  std::uint64_t start = 0;
  std::uint64_t length = 0;

  bool operator==(const StreamSpan&) const = default;
};

// A packed sample selection identifies the bits of one 1/2/4-bit sample inside
// its containing packed byte. bit_offset is counted from the byte's most
// significant bit, matching PNG sample packing.
struct PackedSampleCoordinate {
  std::uint8_t bit_offset = 0;
  std::uint8_t bit_length = 0;

  bool operator==(const PackedSampleCoordinate&) const = default;
};

// Image coordinates. frame is for APNG; pass is the Adam7 pass (0 for
// non-interlaced, 1..7 for Adam7). x/y are image-global coordinates and row is
// the pass-local scanline row. A query may accept zero pass/row as an
// unresolved legacy hint and returns a canonical pass/row in its result. A
// missing channel means the whole pixel is selected. sample_byte is the byte
// within an 8/16-bit sample (0 = first byte);
// packed_sample is mutually exclusive with sample_byte and identifies a
// sub-byte sample. These optional fields make whole-pixel selection distinct
// from channel 0.
struct ImageCoordinate {
  std::uint64_t frame = 0;
  std::uint64_t pass = 0;
  std::uint64_t row = 0;
  std::uint64_t x = 0;
  std::uint64_t y = 0;
  std::optional<std::uint64_t> channel;
  std::optional<std::uint8_t> sample_byte;
  std::optional<PackedSampleCoordinate> packed_sample;

  bool operator==(const ImageCoordinate&) const = default;

  bool valid() const noexcept;
};

// Decode / analysis stage identifiers (ADR-0006 tiered policy).
enum class Stage {
  kFile,       // raw file / chunk envelope
  kChunk,      // chunk semantic node
  kFiltered,   // inflated, still filtered scanlines
  kUnfiltered, // after reverse filtering
  kNative,     // native packed samples / palette index
  kDelivered,  // final display-ready pixels
  kTrace,      // deflate token trace
  kUnknown,
};

const char* stage_text(Stage stage) noexcept;
bool stage_from_text(std::string_view text, Stage& out) noexcept;

struct SemanticNode {
  NodeId id = 0;
  Stage stage = Stage::kUnknown;
  std::vector<BitSpan> physical_spans;

  bool operator==(const SemanticNode&) const = default;
};

// A selection may combine any subset of dimensions; absent dimensions are
// ignored by panels that do not understand them (ADR-0004).
struct Selection {
  std::optional<NodeId> node;
  std::vector<BitSpan> physical_spans;
  std::optional<StreamSpan> logical;
  std::optional<ImageCoordinate> image;
  Stage stage = Stage::kUnknown;

  bool empty() const noexcept;
  bool operator==(const Selection&) const = default;

  // Combines `other` into `*this`: each dimension takes the newer value when
  // present (per-field), physical spans are replaced wholesale by the newer
  // set. Idempotent (merge(a, a) == a) so panels can publish freely.
  void merge_with(const Selection& other) noexcept;
  Selection merged_with(const Selection& other) const noexcept;
};

// Deterministic serialization (locale/order independent). Returns an empty
// string for an empty selection; deserialize(serialize(s)) == s for all s.
std::string serialize(const Selection& selection);

// Parses the output of serialize(); returns std::nullopt on malformed input.
std::optional<Selection> deserialize(std::string_view text);

}  // namespace pnga::trace_model

#endif  // PNGA_TRACE_MODEL_SELECTION_H
