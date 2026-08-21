// WP-203 ArtifactStore: budgeted LRU cache with pinned entries and
// eviction-tracking (rebuildable) keys.

#include "pnga/analysis-engine/artifact_store.h"

#include <algorithm>
#include <vector>

namespace pnga::analysis_engine {

const char* store_error_text(StoreError error) noexcept {
  switch (error) {
    case StoreError::kOk:
      return "ok";
    case StoreError::kNotFound:
      return "not_found";
    case StoreError::kEvicted:
      return "evicted";
    case StoreError::kFormatMismatch:
      return "format_mismatch";
    case StoreError::kTooLarge:
      return "too_large";
  }
  return "unknown";
}

ArtifactStore::ArtifactStore(std::uint64_t budget_bytes)
    : budget_(budget_bytes) {}

StoreError ArtifactStore::put(const pnga::trace_model::ArtifactKey& key,
                              pnga::trace_model::StageArtifact artifact) {
  const std::uint64_t size = artifact.extent().bytes;
  if (size > budget_) {
    return StoreError::kTooLarge;  // cannot fit even alone
  }

  // Replacing an existing materialized entry frees its bytes first.
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    if (!it->second.evicted) {
      used_ -= it->second.size;
    }
    entries_.erase(it);
  }

  Entry entry;
  entry.key = key;
  entry.artifact = std::move(artifact);
  entry.size = size;
  entry.recency = ++clock_;
  entries_.emplace(key, std::move(entry));
  used_ += size;

  evict_until_fits(size);
  // After eviction the just-inserted entry may itself be evicted if the budget
  // cannot hold it; the key then remains marked rebuildable.
  return StoreError::kOk;
}

void ArtifactStore::evict_until_fits(std::uint64_t required) {
  while (used_ > budget_) {
    // Find the least-recently-used unpinned, non-evicted entry.
    Entry* victim = nullptr;
    for (auto& [k, e] : entries_) {
      (void)k;
      if (e.pinned || e.evicted) {
        continue;
      }
      if (victim == nullptr || e.recency < victim->recency) {
        victim = &e;
      }
    }
    if (victim == nullptr) {
      break;  // everything is pinned (or empty) — nothing left to evict
    }
    used_ -= victim->size;  // release the bytes it counted toward the budget
    victim->artifact = pnga::trace_model::StageArtifact{};  // drop the data
    victim->size = 0;
    victim->evicted = true;
  }
}

StoreResult ArtifactStore::get(const pnga::trace_model::ArtifactKey& key) {
  const auto it = entries_.find(key);
  if (it == entries_.end()) {
    return StoreResult{nullptr, StoreError::kNotFound};
  }
  if (it->second.evicted) {
    return StoreResult{nullptr, StoreError::kEvicted};
  }
  it->second.recency = ++clock_;
  return StoreResult{&it->second.artifact, StoreError::kOk};
}

StoreResult ArtifactStore::get(const pnga::trace_model::ArtifactKey& key,
                               pnga::trace_model::ArtifactFormat expected) {
  StoreResult result = get(key);
  if (result.error == StoreError::kOk &&
      result.artifact->format() != expected) {
    result.artifact = nullptr;
    result.error = StoreError::kFormatMismatch;
  }
  return result;
}

void ArtifactStore::pin(const pnga::trace_model::ArtifactKey& key) noexcept {
  const auto it = entries_.find(key);
  if (it != entries_.end()) {
    it->second.pinned = true;
  }
}

void ArtifactStore::unpin(const pnga::trace_model::ArtifactKey& key) noexcept {
  const auto it = entries_.find(key);
  if (it != entries_.end()) {
    it->second.pinned = false;
  }
}

bool ArtifactStore::rebuild_needed(
    const pnga::trace_model::ArtifactKey& key) const noexcept {
  const auto it = entries_.find(key);
  return it != entries_.end() && it->second.evicted;
}

bool ArtifactStore::contains(
    const pnga::trace_model::ArtifactKey& key) const noexcept {
  const auto it = entries_.find(key);
  return it != entries_.end() && !it->second.evicted;
}

}  // namespace pnga::analysis_engine
