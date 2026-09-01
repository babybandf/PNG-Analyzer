// WP-405 persistent index cache tests: durable round-trip, key invalidation,
// truncated/corrupt cache rejection and portable index metadata preservation.

#include <pnga/deflate-index/index_cache.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using pnga::deflate_index::AccessIndexResult;
using pnga::deflate_index::AccessPoint;
using pnga::deflate_index::Adler32Status;
using pnga::deflate_index::BlockIndexResult;
using pnga::deflate_index::BlockType;
using pnga::deflate_index::IndexCache;
using pnga::deflate_index::IndexCacheKey;
using pnga::deflate_index::IndexCacheLookup;
using pnga::deflate_index::PersistentIndexData;
using pnga::deflate_index::ZlibWrapperInfo;

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
  data.blocks.wrapper = ZlibWrapperInfo{0x78, 0x9C, 8, 15, false, true};
  data.blocks.adler.status = Adler32Status::kMatch;
  data.blocks.adler.expected = 0x08962599u;
  data.blocks.adler.actual = 0x08962599u;
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
  REQUIRE(actual.blocks.wrapper == expected.blocks.wrapper);
  REQUIRE(actual.blocks.adler.status == expected.blocks.adler.status);
  REQUIRE(actual.blocks.adler.expected == expected.blocks.adler.expected);
  REQUIRE(actual.blocks.adler.actual == expected.blocks.adler.actual);
  REQUIRE(actual.blocks.stop_input_bit == expected.blocks.stop_input_bit);
  REQUIRE(actual.blocks.stop_output_byte == expected.blocks.stop_output_byte);
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

TEST_CASE("Persistent index cache round-trips every structured block fact",
          "[deflate-index][wp405]") {
  TestCacheRoot temp;
  const IndexCache cache(temp.root);
  const auto cache_key = key();

  // Mismatched checksum with both values and exact stop offsets.
  PersistentIndexData mismatch = index_data();
  mismatch.blocks.adler.status = Adler32Status::kMismatch;
  mismatch.blocks.adler.expected = 0x11223344u;
  mismatch.blocks.adler.actual = 0x55667788u;
  mismatch.blocks.stop_input_bit = 89;
  mismatch.blocks.stop_output_byte = 100;
  REQUIRE(cache.store(cache_key, mismatch).success);
  const auto loaded_mismatch = cache.load(cache_key);
  REQUIRE(loaded_mismatch.status == IndexCacheLookup::kHit);
  require_same_index(loaded_mismatch.data, mismatch);

  // Not-computed checksum with absent values and absent stop offsets.
  PersistentIndexData not_computed = index_data();
  not_computed.blocks.adler.status = Adler32Status::kNotComputed;
  not_computed.blocks.adler.expected = std::nullopt;
  not_computed.blocks.adler.actual = std::nullopt;
  not_computed.blocks.stop_input_bit = std::nullopt;
  not_computed.blocks.stop_output_byte = std::nullopt;
  not_computed.blocks.wrapper.preset_dictionary = true;
  REQUIRE(cache.store(cache_key, not_computed).success);
  const auto loaded_not_computed = cache.load(cache_key);
  REQUIRE(loaded_not_computed.status == IndexCacheLookup::kHit);
  require_same_index(loaded_not_computed.data, not_computed);
}

TEST_CASE("Persistent index cache rejects version 1 cache files",
          "[deflate-index][wp405]") {
  TestCacheRoot temp;
  const IndexCache cache(temp.root);
  const auto cache_key = key();
  REQUIRE(cache.store(cache_key, index_data()).success);
  const auto path = cache.path_for(cache_key);

  // Handcrafted v1 magic suffix: the v2 reader must reject it without
  // publishing any index data.
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(7);
    char magic_suffix = '\0';
    file.read(&magic_suffix, 1);
    REQUIRE(magic_suffix == '\x02');
    file.seekp(7);
    file.put('\x01');
    file.flush();
  }
  const auto v1_magic = cache.load(cache_key);
  REQUIRE(v1_magic.status == IndexCacheLookup::kInvalid);
  REQUIRE_FALSE(v1_magic.hit());
  REQUIRE(v1_magic.data.blocks.blocks.empty());

  // Handcrafted v1 schema number with the v2 magic: also rejected.
  REQUIRE(cache.store(cache_key, index_data()).success);
  {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(8);
    const std::array<char, 4> schema_one = {'\x01', '\x00', '\x00', '\x00'};
    file.write(schema_one.data(), static_cast<std::streamsize>(schema_one.size()));
    file.flush();
  }
  const auto v1_schema = cache.load(cache_key);
  REQUIRE(v1_schema.status == IndexCacheLookup::kInvalid);
  REQUIRE(v1_schema.data.blocks.blocks.empty());
}
