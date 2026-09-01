#ifndef PNGA_DEFLATE_INDEX_INDEX_CACHE_H
#define PNGA_DEFLATE_INDEX_INDEX_CACHE_H

// WP-405: versioned persistent cache for the durable Deflate indexes
// (REPOSITORY_LAYOUT.md §5.6, ADR-0005/0006). Block indexes and portable
// access points may be serialized; process-local inflateCopy snapshots from
// WP-404 are deliberately excluded.

#include <pnga/deflate-index/access_points.h>
#include <pnga/deflate-index/block_index.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace pnga::deflate_index {

// WP-5U12A: schema 2 adds the structured wrapper, Adler-32 and stop facts to
// the durable Block Index payload. Version-1 files are invalid inputs and are
// rebuilt; no compatibility reader invents absent checksum or stop facts.
constexpr std::uint32_t kIndexCacheSchemaVersion = 2;

struct IndexCacheKey {
  // The caller supplies a stable file identity or content hash. A path alone
  // is not sufficient because files can be replaced at the same path.
  std::string file_identity;
  std::string analyzer_schema;
  std::string zlib_version;
  std::string backend_version;
  std::string decode_options;

  bool operator==(const IndexCacheKey&) const = default;
};

struct PersistentIndexData {
  BlockIndexResult blocks;
  AccessIndexResult access;
};

enum class IndexCacheLookup { kHit, kMiss, kInvalid, kIoError };

struct IndexCacheLoadResult {
  IndexCacheLookup status = IndexCacheLookup::kMiss;
  std::string error;
  PersistentIndexData data;

  bool hit() const noexcept { return status == IndexCacheLookup::kHit; }
};

struct IndexCacheStoreResult {
  bool success = false;
  std::string error;
};

// Stores versioned index files below an OS cache directory. The cache never
// writes beside the analyzed PNG. `root` is injectable for deterministic unit
// tests; an empty root selects the platform cache location.
class IndexCache {
 public:
  explicit IndexCache(std::filesystem::path root = {});

  static std::filesystem::path default_root();

  // Exposed for tests and diagnostics. The file name is derived from the full
  // key; the serialized key is also checked on load to reject hash collisions.
  std::filesystem::path path_for(const IndexCacheKey& key) const;

  IndexCacheLoadResult load(const IndexCacheKey& key) const;
  IndexCacheStoreResult store(const IndexCacheKey& key,
                              const PersistentIndexData& data) const;

 private:
  std::filesystem::path root_;
};

}  // namespace pnga::deflate_index

#endif  // PNGA_DEFLATE_INDEX_INDEX_CACHE_H
