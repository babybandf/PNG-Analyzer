// WP-404 session index implementation. Build: feed the whole retained buffer to
// inflate once, capturing an inflateCopy snapshot at output intervals until the
// snapshot memory budget is spent. Extract: restore the nearest snapshot's
// state (its copied z_stream still points into the retained buffer) and run the
// discard/capture loop.

#include "pnga/deflate-index/session_index.h"

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace pnga::deflate_index {

namespace {

constexpr std::size_t kScratchSize = 1 << 16;
constexpr std::size_t kPerSnapshotBytes = 48 * 1024;  // inflateCopy window+state

}  // namespace

SessionIndexResult build_session_index(const std::vector<std::byte>& compressed,
                                       std::uint64_t max_output_bytes,
                                       std::uint64_t interval_bytes,
                                       std::uint64_t snapshot_budget_bytes) {
  SessionIndexResult out;
  if (compressed.empty()) {
    out.error = "empty compressed input";
    return out;
  }
  out.compressed = compressed;

  z_stream strm{};
  if (inflateInit(&strm) != Z_OK) {
    out.error = "inflateInit failed";
    return out;
  }
  strm.next_in = reinterpret_cast<Bytef*>(out.compressed.data());
  strm.avail_in = static_cast<uInt>(out.compressed.size());

  std::vector<std::byte> scratch(kScratchSize);
  std::uint64_t output_total = 0;
  std::uint64_t last_snapshot_output = 0;
  bool have_snapshot = false;
  bool done = false;

  auto capture_if_due = [&](bool force_first) {
    const bool due = !have_snapshot || (force_first && output_total == 0) ||
                     output_total - last_snapshot_output >= interval_bytes;
    if (!due) {
      return;
    }
    // Budget check before capturing (approx size constant per snapshot).
    if (have_snapshot &&
        out.retained_bytes + kPerSnapshotBytes > snapshot_budget_bytes) {
      return;  // budget exhausted: keep fewer (larger replay gaps)
    }
    auto snapshot =
        pnga::deflate_runtime::InflateSnapshot::capture(strm, output_total);
    if (!snapshot.has_value()) {
      return;  // allocation failure: skip, extraction falls back to earlier ones
    }
    out.retained_bytes += snapshot->approx_bytes();
    out.snapshots.push_back(SessionSnapshot{output_total, std::move(*snapshot)});
    last_snapshot_output = output_total;
    have_snapshot = true;
  };

  // Always keep the snapshot at output offset 0.
  capture_if_due(true);

  while (!done) {
    strm.next_out = reinterpret_cast<Bytef*>(scratch.data());
    strm.avail_out = static_cast<uInt>(scratch.size());
    const int ret = inflate(&strm, Z_NO_FLUSH);
    output_total += scratch.size() - strm.avail_out;
    if (output_total > max_output_bytes) {
      out.error = "inflate output cap exceeded";
      inflateEnd(&strm);
      return out;
    }
    if (ret == Z_STREAM_END) {
      out.total_output_bytes = output_total;
      out.adler_ok = true;
      done = true;
    } else if (ret == Z_OK) {
      capture_if_due(false);
      if (strm.avail_in == 0 && output_total != out.total_output_bytes) {
        // No more input but not at end: truncated (should not happen since the
        // whole buffer was fed; inflate would have returned an error).
      }
    } else {
      out.error = ret == Z_DATA_ERROR
                      ? "inflate data error (corrupt stream or bad Adler-32)"
                      : "inflate failed";
      out.adler_ok = ret == Z_DATA_ERROR ? false : out.adler_ok;
      inflateEnd(&strm);
      return out;
    }
  }
  inflateEnd(&strm);
  out.success = true;
  return out;
}

SessionExtractResult extract_session_output(const SessionIndexResult& index,
                                            std::uint64_t output_offset,
                                            std::uint64_t length) {
  SessionExtractResult out;
  if (!index.success || index.snapshots.empty()) {
    out.error = "no session index";
    return out;
  }
  if (output_offset + length > index.total_output_bytes ||
      output_offset + length < output_offset) {
    out.error = "output range out of bounds";
    return out;
  }

  // Nearest snapshot at or before the target.
  std::size_t lo = 0;
  std::size_t hi = index.snapshots.size();
  while (lo + 1 < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (index.snapshots[mid].output_offset <= output_offset) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  const SessionSnapshot& snapshot = index.snapshots[lo];

  z_stream strm{};
  if (inflateInit(&strm) != Z_OK) {
    out.error = "inflateInit failed";
    return out;
  }
  if (!snapshot.state.restore(strm)) {
    inflateEnd(&strm);
    out.error = "snapshot restore failed";
    return out;
  }

  // The restored stream's next_in/avail_in already point into the retained
  // `compressed` buffer; it continues exactly where the snapshot was taken.
  std::vector<std::byte> scratch(kScratchSize);
  std::uint64_t skip = output_offset - snapshot.output_offset;
  out.data.resize(static_cast<std::size_t>(length));
  std::size_t out_pos = 0;
  bool done = false;
  bool ok = true;

  while (out_pos < length && !done) {
    strm.next_out = reinterpret_cast<Bytef*>(scratch.data());
    strm.avail_out = static_cast<uInt>(scratch.size());
    const int ret = inflate(&strm, Z_NO_FLUSH);
    const std::size_t produced = scratch.size() - strm.avail_out;
    if (ret == Z_STREAM_END) {
      done = true;
    } else if (ret != Z_OK) {
      out.error = "inflate failed during session extraction";
      ok = false;
      break;
    }
    std::size_t i = 0;
    if (skip > 0) {
      const std::size_t take =
          static_cast<std::size_t>(std::min<std::uint64_t>(skip, produced));
      skip -= take;
      i += take;
    }
    const std::size_t take =
        std::min<std::size_t>(static_cast<std::size_t>(length) - out_pos,
                              produced - i);
    std::memcpy(out.data.data() + out_pos, scratch.data() + i, take);
    out_pos += take;
  }
  inflateEnd(&strm);

  if (!ok) {
    out.data.clear();
    return out;
  }
  if (out_pos != static_cast<std::size_t>(length)) {
    out.error = "requested output not fully produced";
    out.data.clear();
    return out;
  }
  out.success = true;
  return out;
}

}  // namespace pnga::deflate_index
