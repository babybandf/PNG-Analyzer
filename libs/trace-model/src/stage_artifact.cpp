// WP-203 StageArtifact implementation.

#include "pnga/trace-model/stage_artifact.h"

#include <algorithm>
#include <cstring>

namespace pnga::trace_model {

const char* artifact_format_text(ArtifactFormat format) noexcept {
  switch (format) {
    case ArtifactFormat::kBytes:
      return "bytes";
    case ArtifactFormat::kRows:
      return "rows";
    case ArtifactFormat::kRgba:
      return "rgba";
    case ArtifactFormat::kTile:
      return "tile";
  }
  return "unknown";
}

const char* artifact_backing_text(ArtifactBacking backing) noexcept {
  switch (backing) {
    case ArtifactBacking::kNone:
      return "none";
    case ArtifactBacking::kInline:
      return "inline";
    case ArtifactBacking::kBorrowed:
      return "borrowed";
    case ArtifactBacking::kOwned:
      return "owned";
    case ArtifactBacking::kTileHandle:
      return "tile";
  }
  return "unknown";
}

bool ArtifactKey::operator<(const ArtifactKey& other) const noexcept {
  if (stage != other.stage) {
    return static_cast<int>(stage) < static_cast<int>(other.stage);
  }
  if (row_begin != other.row_begin) {
    return row_begin < other.row_begin;
  }
  return row_end < other.row_end;
}

StageArtifact StageArtifact::owned_bytes(Stage stage,
                                         std::vector<std::byte> data) {
  StageArtifact out;
  out.stage_ = stage;
  out.backing_ = ArtifactBacking::kOwned;
  out.owned_ = std::move(data);
  out.extent_.bytes = out.owned_.size();
  return out;
}

StageArtifact StageArtifact::inline_bytes(Stage stage, const std::byte* data,
                                          std::size_t size) {
  StageArtifact out;
  out.stage_ = stage;
  out.backing_ = ArtifactBacking::kInline;
  const std::size_t n = std::min(size, kInlineCapacity);
  if (n != 0 && data != nullptr) {
    std::memcpy(out.inline_data_.data(), data, n);
  }
  out.inline_size_ = n;
  out.extent_.bytes = n;
  return out;
}

StageArtifact StageArtifact::borrowed_view(Stage stage, const std::byte* data,
                                           std::uint64_t size) {
  StageArtifact out;
  out.stage_ = stage;
  out.backing_ = ArtifactBacking::kBorrowed;
  out.borrowed_ = data;
  out.borrowed_size_ = size;
  out.extent_.bytes = size;
  return out;
}

StageArtifact StageArtifact::tile(Stage stage, std::uint64_t tile_id,
                                  std::uint64_t declared_bytes) {
  StageArtifact out;
  out.stage_ = stage;
  out.format_ = ArtifactFormat::kTile;
  out.backing_ = ArtifactBacking::kTileHandle;
  out.tile_id_ = tile_id;
  out.extent_.bytes = declared_bytes;
  return out;
}

std::optional<std::span<const std::byte>> StageArtifact::bytes() const noexcept {
  switch (backing_) {
    case ArtifactBacking::kInline:
      return std::span<const std::byte>(inline_data_.data(), inline_size_);
    case ArtifactBacking::kBorrowed:
      return std::span<const std::byte>(borrowed_, borrowed_size_);
    case ArtifactBacking::kOwned:
      return std::span<const std::byte>(owned_.data(), owned_.size());
    case ArtifactBacking::kNone:
    case ArtifactBacking::kTileHandle:
      return std::nullopt;
  }
  return std::nullopt;
}

bool StageArtifact::operator==(const StageArtifact& other) const noexcept {
  if (stage_ != other.stage_ || format_ != other.format_ ||
      backing_ != other.backing_ || extent_ != other.extent_ ||
      tile_id_ != other.tile_id_) {
    return false;
  }
  const auto a = bytes();
  const auto b = other.bytes();
  if (a.has_value() != b.has_value()) {
    return false;
  }
  return !a.has_value() ||
         std::equal(a->begin(), a->end(), b->begin(), b->end());
}

}  // namespace pnga::trace_model
