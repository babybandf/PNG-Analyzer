#ifndef PNGA_TRACE_MODEL_STAGE_ARTIFACT_H
#define PNGA_TRACE_MODEL_STAGE_ARTIFACT_H

// WP-203: StageArtifact — one stage's materialized analysis output
// (REPOSITORY_LAYOUT.md §5.4, ADR-0004/0006). Backing is one of inline
// (small owned payload), borrowed view (no ownership), owned buffer, or a
// tile handle into an ArtifactStore. An artifact never holds a GUI object.

#include "pnga/trace-model/selection.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace pnga::trace_model {

// What the artifact bytes represent.
enum class ArtifactFormat { kBytes, kRows, kRgba, kTile };

const char* artifact_format_text(ArtifactFormat format) noexcept;

// Where the artifact data lives.
enum class ArtifactBacking {
  kNone,        // no data (e.g. an evicted entry)
  kInline,      // small payload stored inside the artifact
  kBorrowed,    // mapped view into a longer-lived source; no ownership
  kOwned,       // heap buffer owned by the artifact
  kTileHandle,  // reference to a tile held by an ArtifactStore
};

const char* artifact_backing_text(ArtifactBacking backing) noexcept;

// Spatial extent and byte size of an artifact.
struct ArtifactExtent {
  std::uint64_t width = 0;
  std::uint64_t height = 0;
  std::uint64_t bytes = 0;

  bool operator==(const ArtifactExtent&) const = default;
};

// Identity of an artifact within one document generation.
struct ArtifactKey {
  Stage stage = Stage::kUnknown;
  std::uint64_t row_begin = 0;
  std::uint64_t row_end = 0;  // exclusive

  bool operator==(const ArtifactKey&) const = default;
  bool operator<(const ArtifactKey&) const noexcept;
};

class StageArtifact {
 public:
  static constexpr std::size_t kInlineCapacity = 32;

  static StageArtifact owned_bytes(Stage stage, std::vector<std::byte> data);
  // Copies up to kInlineCapacity bytes; longer input is truncated.
  static StageArtifact inline_bytes(Stage stage, const std::byte* data,
                                    std::size_t size);
  // Borrows `data`; the caller must keep it alive for the artifact's lifetime.
  static StageArtifact borrowed_view(Stage stage, const std::byte* data,
                                     std::uint64_t size);
  static StageArtifact tile(Stage stage, std::uint64_t tile_id,
                            std::uint64_t declared_bytes);

  Stage stage() const noexcept { return stage_; }
  ArtifactFormat format() const noexcept { return format_; }
  ArtifactBacking backing() const noexcept { return backing_; }
  const ArtifactExtent& extent() const noexcept { return extent_; }

  // Data view when materialized locally (inline/borrowed/owned).
  std::optional<std::span<const std::byte>> bytes() const noexcept;

  // True when this artifact carries data (not a tile handle, not empty).
  bool materialized() const noexcept { return backing_ != ArtifactBacking::kNone && backing_ != ArtifactBacking::kTileHandle; }
  bool owns_data() const noexcept {
    return backing_ == ArtifactBacking::kInline ||
           backing_ == ArtifactBacking::kOwned;
  }
  std::uint64_t tile_id() const noexcept { return tile_id_; }

  // Metadata + content equality (used by tests and merge comparisons).
  bool operator==(const StageArtifact& other) const noexcept;

  void set_format(ArtifactFormat format) noexcept { format_ = format; }
  void set_extent(ArtifactExtent extent) noexcept { extent_ = extent; }

 private:
  Stage stage_ = Stage::kUnknown;
  ArtifactFormat format_ = ArtifactFormat::kBytes;
  ArtifactBacking backing_ = ArtifactBacking::kNone;
  ArtifactExtent extent_;

  std::array<std::byte, kInlineCapacity> inline_data_{};
  std::size_t inline_size_ = 0;
  const std::byte* borrowed_ = nullptr;
  std::uint64_t borrowed_size_ = 0;
  std::vector<std::byte> owned_;
  std::uint64_t tile_id_ = 0;
};

}  // namespace pnga::trace_model

#endif  // PNGA_TRACE_MODEL_STAGE_ARTIFACT_H
