// WP-405 persistent index cache tests: durable round-trip, key invalidation,
// truncated/corrupt cache rejection and portable index metadata preservation.

#include <pnga/deflate-index/index_cache.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using pnga::deflate_index::AccessIndexResult;
using pnga::deflate_index::AccessPoint;
using pnga::deflate_index::BlockIndexResult;
using pnga::deflate_index::BlockType;
using pnga::deflate_index::IndexCache;
using pnga::deflate_index::IndexCacheKey;
using pnga::deflate_index::IndexCacheLookup;
using pnga::deflate_index::PersistentIndexData;

std::byte B(unsigned int value) {
  return static_cast<std::byte>(value & 0xFFu);
}

struct TestCacheRoot {
  TestCacheRoot() {
    root = std::filesystem::temp_directory_path() /
           "pnga-index-cache-wp405";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  ~TestCacheRoot() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  std::filesystem::path root;
};

IndexCacheKey key() {
  return IndexCacheKey{"png-content-hash-1", "schema-1", "1.3.2", "0.1.0",
                       "interlace=1;max-output=1048576"};
}

PersistentIndexData index_data() {
  PersistentIndexData data;
  data.blocks.success = true;
  data.blocks.zlib_header_bits = 16;
  data.blocks.total_output_bytes = 100;
  data.blocks.adler_ok = true;
  data.blocks.blocks = {
      {0, BlockType::kFixed, false, 16, 37, 0, 64},
      {1, BlockType::kDynamic, true, 37, 89, 64, 100},
  };

  data.access.success = true;
  data.access.total_output_bytes = 100;
  data.access.zlib_header = {B(0x78), B(0x9C)};
  data.access.adler_ok = true;
  AccessPoint first;
  first.input_byte = 2;
  first.output_offset = 0;
  AccessPoint second;
  second.input_byte = 8;
  second.prime_bits = 3;
  second.prime_value = 5;
  second.output_offset = 64;
  second.dictionary = {B(1), B(2), B(3), B(4)};
  data.access.points = {std::move(first), std::move(second)};
  return data;
}

void require_same_index(const PersistentIndexData& actual,
                        const PersistentIndexData& expected) {
  REQUIRE(actual.blocks.success);
  REQUIRE(actual.blocks.zlib_header_bits == expected.blocks.zlib_header_bits);
  REQUIRE(actual.blocks.total_output_bytes ==
          expected.blocks.total_output_bytes);
  REQUIRE(actual.blocks.adler_ok == expected.blocks.adler_ok);
  REQUIRE(actual.blocks.blocks.size() == expected.blocks.blocks.size());
  for (std::size_t i = 0; i < expected.blocks.blocks.size(); ++i) {
    REQUIRE(actual.blocks.blocks[i].index == expected.blocks.blocks[i].index);
    REQUIRE(actual.blocks.blocks[i].type == expected.blocks.blocks[i].type);
    REQUIRE(actual.blocks.blocks[i].last == expected.blocks.blocks[i].last);
    REQUIRE(actual.blocks.blocks[i].input_bit_begin ==
            expected.blocks.blocks[i].input_bit_begin);
    REQUIRE(actual.blocks.blocks[i].input_bit_end ==
            expected.blocks.blocks[i].input_bit_end);
    REQUIRE(actual.blocks.blocks[i].output_begin ==
            expected.blocks.blocks[i].output_begin);
    REQUIRE(actual.blocks.blocks[i].output_end ==
            expected.blocks.blocks[i].output_end);
  }
  REQUIRE(actual.access.success);
  REQUIRE(actual.access.total_output_bytes == expected.access.total_output_bytes);
  REQUIRE(actual.access.zlib_header == expected.access.zlib_header);
  REQUIRE(actual.access.adler_ok == expected.access.adler_ok);
  REQUIRE(actual.access.points.size() == expected.access.points.size());
  for (std::size_t i = 0; i < expected.access.points.size(); ++i) {
    const auto& a = actual.access.points[i];
    const auto& e = expected.access.points[i];
    REQUIRE(a.input_byte == e.input_byte);
    REQUIRE(a.prime_bits == e.prime_bits);
    REQUIRE(a.prime_value == e.prime_value);
    REQUIRE(a.output_offset == e.output_offset);
    REQUIRE(a.dictionary == e.dictionary);
  }
}

}  // namespace

TEST_CASE("Persistent index cache round-trips durable indexes",
          "[deflate-index][wp405]") {
  TestCacheRoot temp;
  const IndexCache cache(temp.root);
  const auto cache_key = key();
  const auto expected = index_data();

  const auto stored = cache.store(cache_key, expected);
  REQUIRE(stored.success);

  const auto loaded = cache.load(cache_key);
  REQUIRE(loaded.status == IndexCacheLookup::kHit);
  require_same_index(loaded.data, expected);
}

TEST_CASE("Persistent index cache misses when any key dimension changes",
          "[deflate-index][wp405]") {
  TestCacheRoot temp;
  const IndexCache cache(temp.root);
  const auto original = key();
  REQUIRE(cache.store(original, index_data()).success);

  const std::array<std::string, 5> dimensions = {
      "file-content-hash-2", "schema-2", "1.3.3", "0.2.0",
      "interlace=0;max-output=1048576"};
  for (std::size_t i = 0; i < dimensions.size(); ++i) {
    auto changed = original;
    switch (i) {
      case 0:
        changed.file_identity = dimensions[i];
        break;
      case 1:
        changed.analyzer_schema = dimensions[i];
        break;
      case 2:
        changed.zlib_version = dimensions[i];
        break;
      case 3:
        changed.backend_version = dimensions[i];
        break;
      case 4:
        changed.decode_options = dimensions[i];
        break;
    }
    REQUIRE(cache.load(changed).status == IndexCacheLookup::kMiss);
  }
}

TEST_CASE("Persistent index cache rejects truncated and corrupt files",
          "[deflate-index][wp405]") {
  TestCacheRoot temp;
  const IndexCache cache(temp.root);
  const auto cache_key = key();
  REQUIRE(cache.store(cache_key, index_data()).success);

  {
    std::ofstream output(cache.path_for(cache_key),
                         std::ios::binary | std::ios::trunc);
    output << "PNGAIDX";
  }
  const auto truncated = cache.load(cache_key);
  REQUIRE(truncated.status == IndexCacheLookup::kInvalid);

  REQUIRE(cache.store(cache_key, index_data()).success);
  {
    std::ofstream output(cache.path_for(cache_key),
                         std::ios::binary | std::ios::trunc);
    output << "not-a-pnga-index-cache";
  }
  const auto corrupt = cache.load(cache_key);
  REQUIRE(corrupt.status == IndexCacheLookup::kInvalid);
}

TEST_CASE("Persistent index cache does not retain session snapshots",
          "[deflate-index][wp405]") {
  TestCacheRoot temp;
  const IndexCache cache(temp.root);
  REQUIRE(cache.store(key(), index_data()).success);

  const auto loaded = cache.load(key());
  REQUIRE(loaded.status == IndexCacheLookup::kHit);
  // The durable payload contains only block/access indexes; no z_stream or
  // session snapshot type is present in the public cache data.
  REQUIRE(loaded.data.blocks.blocks.size() == 2);
  REQUIRE(loaded.data.access.points.size() == 2);
}
