// WP-203 ArtifactStore tests: budget eviction (LRU), pinned entries are never
// evicted, evicted keys stay rebuildable, format mismatch is rejected, and
// oversized artifacts are refused.

#include <pnga/analysis-engine/artifact_store.h>

#include <pnga/trace-model/stage_artifact.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using pnga::analysis_engine::ArtifactStore;
using pnga::analysis_engine::StoreError;
using pnga::analysis_engine::StoreResult;
using pnga::trace_model::ArtifactFormat;
using pnga::trace_model::ArtifactKey;
using pnga::trace_model::Stage;
using pnga::trace_model::StageArtifact;

namespace {

std::vector<std::byte> bytes_of(std::size_t n, unsigned char fill = 0x11) {
  return std::vector<std::byte>(n, static_cast<std::byte>(fill));
}

ArtifactKey key(Stage stage, std::uint64_t row = 0) {
  return ArtifactKey{stage, row, row + 1};
}

}  // namespace

TEST_CASE("Artifacts fit under the budget and are retrievable",
          "[analysis-engine][wp203]") {
  ArtifactStore store(100);
  REQUIRE(store.put(key(Stage::kFiltered),
                    StageArtifact::owned_bytes(Stage::kFiltered,
                                               bytes_of(40)))
              == StoreError::kOk);
  REQUIRE(store.used_bytes() == 40);

  const StoreResult r = store.get(key(Stage::kFiltered));
  REQUIRE(r);
  REQUIRE(r.artifact->backing() ==
          pnga::trace_model::ArtifactBacking::kOwned);
  REQUIRE(r.artifact->bytes().has_value());
  REQUIRE(r.artifact->bytes()->size() == 40);
}

TEST_CASE("Over-budget insertion evicts least-recently-used entries",
          "[analysis-engine][wp203]") {
  ArtifactStore store(80);
  REQUIRE(store.put(key(Stage::kFiltered, 0),
                    StageArtifact::owned_bytes(Stage::kFiltered, bytes_of(30)))
              == StoreError::kOk);
  REQUIRE(store.put(key(Stage::kFiltered, 1),
                    StageArtifact::owned_bytes(Stage::kFiltered, bytes_of(30)))
              == StoreError::kOk);

  // Touch the first key so the second is older; then insert a 40-byte entry
  // that exceeds the budget, forcing the LRU (second) entry out.
  (void)store.get(key(Stage::kFiltered, 0));
  REQUIRE(store.put(key(Stage::kNative),
                    StageArtifact::owned_bytes(Stage::kNative, bytes_of(40)))
              == StoreError::kOk);

  REQUIRE(store.contains(key(Stage::kFiltered, 0)));
  REQUIRE_FALSE(store.contains(key(Stage::kFiltered, 1)));
  REQUIRE(store.get(key(Stage::kFiltered, 1)).error == StoreError::kEvicted);
  REQUIRE(store.rebuild_needed(key(Stage::kFiltered, 1)));
  REQUIRE_FALSE(store.rebuild_needed(key(Stage::kFiltered, 0)));
  REQUIRE(store.used_bytes() == 70);
}

TEST_CASE("Pinned entries are never evicted", "[analysis-engine][wp203]") {
  ArtifactStore store(80);
  const auto akey = key(Stage::kFiltered, 0);
  const auto bkey = key(Stage::kFiltered, 1);
  REQUIRE(store.put(akey,
                    StageArtifact::owned_bytes(Stage::kFiltered, bytes_of(30)))
              == StoreError::kOk);
  REQUIRE(store.put(bkey,
                    StageArtifact::owned_bytes(Stage::kFiltered, bytes_of(30)))
              == StoreError::kOk);
  store.pin(akey);
  // Inserting another 30-byte entry cannot evict the pinned one, so it evicts
  // bkey; afterwards the pinned akey must still be materialized.
  REQUIRE(store.put(key(Stage::kNative),
                    StageArtifact::owned_bytes(Stage::kNative, bytes_of(30)))
              == StoreError::kOk);
  REQUIRE(store.contains(akey));
  REQUIRE_FALSE(store.contains(bkey));

  store.unpin(akey);
  // Now akey is evictable and a fresh large insert can evict it (it is the
  // least-recently-used unpinned entry).
  REQUIRE(store.put(key(Stage::kDelivered),
                    StageArtifact::owned_bytes(Stage::kDelivered, bytes_of(30)))
              == StoreError::kOk);
  REQUIRE_FALSE(store.contains(akey));
}

TEST_CASE("A re-put replaces an evicted entry and clears the rebuild mark",
          "[analysis-engine][wp203]") {
  ArtifactStore store(60);
  const auto akey = key(Stage::kFiltered, 0);
  REQUIRE(store.put(akey,
                    StageArtifact::owned_bytes(Stage::kFiltered, bytes_of(50)))
              == StoreError::kOk);
  // Force eviction of akey.
  REQUIRE(store.put(key(Stage::kNative),
                    StageArtifact::owned_bytes(Stage::kNative, bytes_of(50)))
              == StoreError::kOk);
  REQUIRE(store.rebuild_needed(akey));

  REQUIRE(store.put(akey,
                    StageArtifact::owned_bytes(Stage::kFiltered, bytes_of(10)))
              == StoreError::kOk);
  REQUIRE_FALSE(store.rebuild_needed(akey));
  REQUIRE(store.contains(akey));
  REQUIRE(store.get(akey).artifact->bytes()->size() == 10);
}

TEST_CASE("Format mismatch is rejected on typed get", "[analysis-engine][wp203]") {
  ArtifactStore store(100);
  auto art = StageArtifact::owned_bytes(Stage::kDelivered, bytes_of(16));
  art.set_format(ArtifactFormat::kRgba);
  const auto akey = key(Stage::kDelivered);
  REQUIRE(store.put(akey, std::move(art)) == StoreError::kOk);

  REQUIRE(store.get(akey, ArtifactFormat::kRgba).error == StoreError::kOk);
  REQUIRE(store.get(akey, ArtifactFormat::kRows).error ==
          StoreError::kFormatMismatch);
  REQUIRE(store.get(akey, ArtifactFormat::kBytes).error ==
          StoreError::kFormatMismatch);
}

TEST_CASE("An oversized artifact is refused and not inserted",
          "[analysis-engine][wp203]") {
  ArtifactStore store(16);
  REQUIRE(store.put(key(Stage::kFiltered),
                    StageArtifact::owned_bytes(Stage::kFiltered, bytes_of(32)))
              == StoreError::kTooLarge);
  REQUIRE(store.entry_count() == 0);
  REQUIRE(store.get(key(Stage::kFiltered)).error == StoreError::kNotFound);
}

TEST_CASE("All-backing variants materialize or report tile handles",
          "[analysis-engine][wp203]") {
  const std::byte raw[8] = {std::byte{1}, std::byte{2}, std::byte{3},
                            std::byte{4}, std::byte{5}, std::byte{6},
                            std::byte{7}, std::byte{8}};

  const auto owned =
      StageArtifact::owned_bytes(Stage::kFiltered,
                                 std::vector<std::byte>(raw, raw + 8));
  REQUIRE(owned.owns_data());
  REQUIRE(owned.bytes()->size() == 8);
  REQUIRE((*owned.bytes())[0] == std::byte{1});

  const auto inl =
      StageArtifact::inline_bytes(Stage::kChunk, raw, 8);
  REQUIRE(inl.backing() == pnga::trace_model::ArtifactBacking::kInline);
  REQUIRE(inl.owns_data());
  REQUIRE(inl.bytes()->size() == 8);

  const auto borrowed = StageArtifact::borrowed_view(Stage::kFile, raw, 8);
  REQUIRE_FALSE(borrowed.owns_data());
  REQUIRE(borrowed.materialized());
  REQUIRE(borrowed.bytes()->size() == 8);

  const auto t = StageArtifact::tile(Stage::kDelivered, 42, 4096);
  REQUIRE_FALSE(t.materialized());
  REQUIRE(t.tile_id() == 42);
  REQUIRE_FALSE(t.bytes().has_value());
}
