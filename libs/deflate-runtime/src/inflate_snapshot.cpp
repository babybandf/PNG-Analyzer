// WP-404 inflateCopy snapshot implementation. The snapshot owns a heap-allocated
// z_stream produced by inflateCopy; restore() copies it back into a live stream.

#include "pnga/deflate-runtime/inflate_snapshot.h"

#include <utility>

namespace pnga::deflate_runtime {

// Rough retained size of an inflateCopy state: z_stream + 32 KiB window plus
// the huffman/state tables. Used only for budget accounting.
constexpr std::size_t kApproxSnapshotBytes = 48 * 1024;

struct InflateSnapshot::Impl {
  z_stream stream{};

  ~Impl() {
    if (stream.state != nullptr) {
      inflateEnd(&stream);
    }
  }
};

InflateSnapshot::InflateSnapshot() = default;

InflateSnapshot::~InflateSnapshot() = default;

InflateSnapshot::InflateSnapshot(InflateSnapshot&& other) noexcept
    : impl_(std::move(other.impl_)), output_offset_(other.output_offset_) {
  other.output_offset_ = 0;
}

InflateSnapshot& InflateSnapshot::operator=(InflateSnapshot&& other) noexcept {
  if (this != &other) {
    impl_ = std::move(other.impl_);
    output_offset_ = other.output_offset_;
    other.output_offset_ = 0;
  }
  return *this;
}

std::optional<InflateSnapshot> InflateSnapshot::capture(z_stream& stream,
                                                        std::uint64_t output_offset) {
  InflateSnapshot snapshot;
  snapshot.impl_ = std::make_unique<Impl>();
  if (inflateCopy(&snapshot.impl_->stream, &stream) != Z_OK) {
    return std::nullopt;
  }
  snapshot.output_offset_ = output_offset;
  return snapshot;
}

bool InflateSnapshot::restore(z_stream& dst) const {
  if (impl_ == nullptr) {
    return false;
  }
  // inflateCopy does not release an already initialized destination. Release
  // it first, then copy directly into `dst`: zlib stores a back-pointer to the
  // destination z_stream in the copied internal state, so a shallow assignment
  // from a temporary z_stream would leave that pointer dangling.
  if (dst.state != nullptr) {
    inflateEnd(&dst);
  }
  return inflateCopy(&dst, &impl_->stream) == Z_OK;
}

std::size_t InflateSnapshot::approx_bytes() const noexcept {
  return impl_ != nullptr ? kApproxSnapshotBytes : 0;
}

}  // namespace pnga::deflate_runtime
