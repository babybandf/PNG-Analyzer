#ifndef PNGA_ANALYSIS_ENGINE_ARTIFACT_STORE_H
#define PNGA_ANALYSIS_ENGINE_ARTIFACT_STORE_H

// WP-203: ArtifactStore — memory-budgeted cache of StageArtifacts keyed by
// (stage, row range). Eviction is least-recently-used and skips pinned
// entries; evicted keys keep their metadata and are marked rebuildable so a
// caller can regenerate them (ADR-0006). No GUI objects are stored.

#include <pnga/trace-model/stage_artifact.h>

#include <cstdint>
#include <map>

namespace pnga::analysis_engine {

enum class StoreError { kOk, kNotFound, kEvicted, kFormatMismatch, kTooLarge };

const char* store_error_text(StoreError error) noexcept;

struct StoreResult {
  const pnga::trace_model::StageArtifact* artifact = nullptr;
  StoreError error = StoreError::kNotFound;

  explicit operator bool() const noexcept {
    return error == StoreError::kOk;
  }
};

class ArtifactStore {
 public:
  explicit ArtifactStore(std::uint64_t budget_bytes);

  std::uint64_t budget_bytes() const noexcept { return budget_; }
  std::uint64_t used_bytes() const noexcept { return used_; }

  // Inserts `artifact` under `key`, evicting least-recently-used unpinned
  // entries until the budget fits. Returns kTooLarge (nothing inserted) when
  // the artifact alone exceeds the budget or no evictable space can be made.
  StoreError put(const pnga::trace_model::ArtifactKey& key,
                 pnga::trace_model::StageArtifact artifact);

  // Returns the artifact for `key` and bumps its recency. kNotFound when
  // absent, kEvicted when the data was released but can be rebuilt. Not const:
  // a cache lookup updates LRU recency.
  StoreResult get(const pnga::trace_model::ArtifactKey& key);

  // Like get(), but kFormatMismatch when the stored artifact's format differs
  // from `expected`.
  StoreResult get(const pnga::trace_model::ArtifactKey& key,
                  pnga::trace_model::ArtifactFormat expected);

  // Pinned entries are never evicted; callers must unpin when done.
  void pin(const pnga::trace_model::ArtifactKey& key) noexcept;
  void unpin(const pnga::trace_model::ArtifactKey& key) noexcept;

  // True when `key` exists but its data was evicted and must be rebuilt.
  bool rebuild_needed(const pnga::trace_model::ArtifactKey& key) const noexcept;

  // True when `key` is currently materialized in the store.
  bool contains(const pnga::trace_model::ArtifactKey& key) const noexcept;

  std::size_t entry_count() const noexcept { return entries_.size(); }

 private:
  struct Entry {
    pnga::trace_model::ArtifactKey key;
    pnga::trace_model::StageArtifact artifact;
    std::uint64_t size = 0;
    bool pinned = false;
    bool evicted = false;
    std::uint64_t recency = 0;
  };

  void evict_until_fits(std::uint64_t required);

  std::uint64_t budget_;
  std::uint64_t used_ = 0;
  std::uint64_t clock_ = 0;
  std::map<pnga::trace_model::ArtifactKey, Entry> entries_;
};

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_ARTIFACT_STORE_H
