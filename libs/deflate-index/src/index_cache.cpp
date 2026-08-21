// WP-405 versioned persistent cache for block/access indexes. Cache files are
// untrusted input: parsing is bounded before any vector allocation, and a bad
// cache is reported as a miss candidate rather than trusted or published.

#include "pnga/deflate-index/index_cache.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace pnga::deflate_index {

namespace {

constexpr std::array<char, 8> kMagic = {'P', 'N', 'G', 'A', 'I', 'D', 'X', 1};
constexpr std::uint64_t kMaxCacheBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxEntries = 1'000'000;
constexpr std::uint32_t kMaxStringBytes = 4096;

bool valid_key(const IndexCacheKey& key) noexcept {
  return key.file_identity.size() <= kMaxStringBytes &&
         key.analyzer_schema.size() <= kMaxStringBytes &&
         key.zlib_version.size() <= kMaxStringBytes &&
         key.backend_version.size() <= kMaxStringBytes &&
         key.decode_options.size() <= kMaxStringBytes;
}

std::uint64_t fnv1a_append(std::uint64_t hash, const std::byte* data,
                           std::size_t size) noexcept {
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint8_t>(data[i]);
    hash *= kPrime;
  }
  return hash;
}

std::uint64_t fnv1a_append_string(std::uint64_t hash,
                                  const std::string& value) noexcept {
  const std::uint64_t length = static_cast<std::uint64_t>(value.size());
  std::array<std::byte, sizeof(length)> encoded{};
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    encoded[i] = static_cast<std::byte>((length >> (i * 8)) & 0xFFu);
  }
  hash = fnv1a_append(hash, encoded.data(), encoded.size());
  return fnv1a_append(hash,
                      reinterpret_cast<const std::byte*>(value.data()),
                      value.size());
}

std::string hex_u64(std::uint64_t value) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string result(16, '0');
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[result.size() - 1 - i] = kHex[value & 0xFu];
    value >>= 4;
  }
  return result;
}

std::string key_digest(const IndexCacheKey& key) {
  std::uint64_t hash = 1469598103934665603ULL;
  hash = fnv1a_append_string(hash, key.file_identity);
  hash = fnv1a_append_string(hash, key.analyzer_schema);
  hash = fnv1a_append_string(hash, key.zlib_version);
  hash = fnv1a_append_string(hash, key.backend_version);
  hash = fnv1a_append_string(hash, key.decode_options);
  return hex_u64(hash);
}

class Writer {
 public:
  bool byte(std::uint8_t value) {
    const std::array<std::byte, 1> encoded = {
        static_cast<std::byte>(value)};
    return bytes(encoded);
  }

  bool u32(std::uint32_t value) {
    std::array<std::byte, 4> encoded{};
    for (std::size_t i = 0; i < encoded.size(); ++i) {
      encoded[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFu);
    }
    return bytes(encoded);
  }

  bool u64(std::uint64_t value) {
    std::array<std::byte, 8> encoded{};
    for (std::size_t i = 0; i < encoded.size(); ++i) {
      encoded[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFu);
    }
    return bytes(encoded);
  }

  bool string(const std::string& value) {
    if (value.size() > kMaxStringBytes ||
        value.size() > std::numeric_limits<std::uint32_t>::max() ||
        !u32(static_cast<std::uint32_t>(value.size()))) {
      return false;
    }
    return bytes(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(value.data()), value.size()));
  }

  bool bytes(std::span<const std::byte> value) {
    if (data_.size() > kMaxCacheBytes ||
        value.size() > kMaxCacheBytes - data_.size()) {
      return false;
    }
    data_.insert(data_.end(), value.begin(), value.end());
    return true;
  }

  std::vector<std::byte> take() && { return std::move(data_); }

 private:
  std::vector<std::byte> data_;
};

class Reader {
 public:
  explicit Reader(std::span<const std::byte> data) : data_(data) {}

  bool magic() {
    if (remaining() < kMagic.size() ||
        std::memcmp(data_.data() + position_, kMagic.data(), kMagic.size()) !=
            0) {
      return false;
    }
    position_ += kMagic.size();
    return true;
  }

  bool byte(std::uint8_t& value) {
    if (remaining() < 1) {
      return false;
    }
    value = static_cast<std::uint8_t>(data_[position_++]);
    return true;
  }

  bool u32(std::uint32_t& value) {
    if (remaining() < 4) {
      return false;
    }
    value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(
                   static_cast<std::uint8_t>(data_[position_++]))
               << (i * 8);
    }
    return true;
  }

  bool u64(std::uint64_t& value) {
    if (remaining() < 8) {
      return false;
    }
    value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
      value |= static_cast<std::uint64_t>(
                   static_cast<std::uint8_t>(data_[position_++]))
               << (i * 8);
    }
    return true;
  }

  bool string(std::string& value) {
    std::uint32_t size = 0;
    if (!u32(size) || size > kMaxStringBytes || remaining() < size) {
      return false;
    }
    value.assign(reinterpret_cast<const char*>(data_.data() + position_), size);
    position_ += size;
    return true;
  }

  bool bytes(std::size_t size, std::vector<std::byte>& value) {
    if (size > remaining()) {
      return false;
    }
    value.assign(data_.begin() + static_cast<std::ptrdiff_t>(position_),
                 data_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
    position_ += size;
    return true;
  }

  bool bytes(std::span<std::byte> value) {
    if (value.size() > remaining()) {
      return false;
    }
    std::memcpy(value.data(), data_.data() + position_, value.size());
    position_ += value.size();
    return true;
  }

  std::size_t remaining() const noexcept { return data_.size() - position_; }

 private:
  std::span<const std::byte> data_;
  std::size_t position_ = 0;
};

bool write_key(Writer& writer, const IndexCacheKey& key) {
  return writer.string(key.file_identity) &&
         writer.string(key.analyzer_schema) &&
         writer.string(key.zlib_version) &&
         writer.string(key.backend_version) &&
         writer.string(key.decode_options);
}

bool read_key(Reader& reader, IndexCacheKey& key) {
  return reader.string(key.file_identity) &&
         reader.string(key.analyzer_schema) &&
         reader.string(key.zlib_version) &&
         reader.string(key.backend_version) &&
         reader.string(key.decode_options);
}

bool write_data(Writer& writer, const PersistentIndexData& data) {
  if (!data.blocks.success || !data.access.success ||
      data.blocks.blocks.size() > kMaxEntries ||
      data.access.points.size() > kMaxEntries) {
    return false;
  }
  if (!writer.u64(data.blocks.blocks.size())) {
    return false;
  }
  for (const auto& block : data.blocks.blocks) {
    if (static_cast<std::uint8_t>(block.type) > 2 ||
        block.input_bit_begin > block.input_bit_end ||
        block.output_begin > block.output_end || !writer.u64(block.index) ||
        !writer.byte(static_cast<std::uint8_t>(block.type)) ||
        !writer.byte(block.last ? 1 : 0) || !writer.u64(block.input_bit_begin) ||
        !writer.u64(block.input_bit_end) || !writer.u64(block.output_begin) ||
        !writer.u64(block.output_end)) {
      return false;
    }
  }
  if (!writer.u64(data.blocks.zlib_header_bits) ||
      !writer.u64(data.blocks.total_output_bytes) ||
      !writer.byte(data.blocks.adler_ok ? 1 : 0) ||
      !writer.u64(data.access.points.size())) {
    return false;
  }
  for (const auto& point : data.access.points) {
    if (point.prime_bits > 7 || point.dictionary.size() > kWindowSize ||
        (point.output_offset == 0 && !point.dictionary.empty()) ||
        point.dictionary.size() > std::numeric_limits<std::uint32_t>::max() ||
        !writer.u64(point.input_byte) || !writer.byte(point.prime_bits) ||
        !writer.byte(point.prime_value) || !writer.u64(point.output_offset) ||
        !writer.u32(static_cast<std::uint32_t>(point.dictionary.size())) ||
        !writer.bytes(point.dictionary)) {
      return false;
    }
  }
  return writer.u64(data.access.total_output_bytes) &&
         writer.bytes(data.access.zlib_header) &&
         writer.byte(data.access.adler_ok ? 1 : 0);
}

bool read_data(Reader& reader, PersistentIndexData& data) {
  constexpr std::size_t kMinimumBlockRecordBytes = 42;
  constexpr std::size_t kMinimumAccessRecordBytes = 22;
  constexpr std::size_t kAccessFooterBytes = 11;

  std::uint64_t count = 0;
  if (!reader.u64(count) || count > kMaxEntries ||
      count > reader.remaining() / kMinimumBlockRecordBytes) {
    return false;
  }
  data.blocks.blocks.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t i = 0; i < count; ++i) {
    DeflateBlock block;
    std::uint8_t type = 0;
    std::uint8_t last = 0;
    if (!reader.u64(block.index) || !reader.byte(type) || !reader.byte(last) ||
        !reader.u64(block.input_bit_begin) ||
        !reader.u64(block.input_bit_end) ||
        !reader.u64(block.output_begin) || !reader.u64(block.output_end) ||
        type > 2 || last > 1 || block.input_bit_begin > block.input_bit_end ||
        block.output_begin > block.output_end) {
      return false;
    }
    block.type = static_cast<BlockType>(type);
    block.last = last != 0;
    data.blocks.blocks.push_back(std::move(block));
  }
  std::uint8_t adler = 0;
  if (!reader.u64(data.blocks.zlib_header_bits) ||
      !reader.u64(data.blocks.total_output_bytes) || !reader.byte(adler) ||
      adler > 1 || !reader.u64(count) || count == 0 ||
      count > kMaxEntries || reader.remaining() < kAccessFooterBytes ||
      count > (reader.remaining() - kAccessFooterBytes) /
                  kMinimumAccessRecordBytes) {
    return false;
  }
  data.blocks.adler_ok = adler != 0;

  data.access.points.reserve(static_cast<std::size_t>(count));
  std::uint64_t previous_output = 0;
  bool first = true;
  for (std::uint64_t i = 0; i < count; ++i) {
    AccessPoint point;
    std::uint32_t dictionary_bytes = 0;
    if (!reader.u64(point.input_byte) || !reader.byte(point.prime_bits) ||
        !reader.byte(point.prime_value) || !reader.u64(point.output_offset) ||
        !reader.u32(dictionary_bytes) || point.prime_bits > 7 ||
        dictionary_bytes > kWindowSize ||
        (point.output_offset == 0 && dictionary_bytes != 0) ||
        (!first && point.output_offset < previous_output) ||
        !reader.bytes(dictionary_bytes, point.dictionary)) {
      return false;
    }
    previous_output = point.output_offset;
    first = false;
    data.access.points.push_back(std::move(point));
  }
  if (first || data.access.points.front().output_offset != 0) {
    return false;
  }
  std::uint8_t access_adler = 0;
  if (!reader.u64(data.access.total_output_bytes) ||
      !reader.bytes(data.access.zlib_header) || !reader.byte(access_adler) ||
      access_adler > 1) {
    return false;
  }
  data.access.adler_ok = access_adler != 0;
  return reader.remaining() == 0;
}

bool serialize(const IndexCacheKey& key, const PersistentIndexData& data,
               std::vector<std::byte>& output) {
  if (!valid_key(key)) {
    return false;
  }
  Writer writer;
  if (!writer.bytes(std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(kMagic.data()), kMagic.size())) ||
      !writer.u32(kIndexCacheSchemaVersion) || !writer.u32(0) ||
      !write_key(writer, key) || !write_data(writer, data)) {
    return false;
  }
  output = std::move(writer).take();
  return true;
}

bool deserialize(std::span<const std::byte> bytes, const IndexCacheKey& expected,
                 PersistentIndexData& output) {
  Reader reader(bytes);
  if (!reader.magic()) {
    return false;
  }
  std::uint32_t schema = 0;
  std::uint32_t reserved = 0;
  if (!reader.u32(schema) || !reader.u32(reserved) ||
      schema != kIndexCacheSchemaVersion || reserved != 0) {
    return false;
  }
  IndexCacheKey stored;
  if (!read_key(reader, stored) || !(stored == expected) ||
      !read_data(reader, output)) {
    return false;
  }
  output.blocks.success = true;
  output.access.success = true;
  return true;
}

std::optional<std::string> environment_value(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

}  // namespace

IndexCache::IndexCache(std::filesystem::path root)
    : root_(root.empty() ? default_root() : std::move(root)) {}

std::filesystem::path IndexCache::default_root() {
  std::filesystem::path base;
#ifdef _WIN32
  if (const auto local = environment_value("LOCALAPPDATA"); local.has_value()) {
    base = *local;
  }
#elif defined(__APPLE__)
  if (const auto home = environment_value("HOME"); home.has_value()) {
    base = std::filesystem::path(*home) / "Library" / "Caches";
  }
#else
  if (const auto xdg = environment_value("XDG_CACHE_HOME"); xdg.has_value()) {
    base = *xdg;
  } else if (const auto home = environment_value("HOME"); home.has_value()) {
    base = std::filesystem::path(*home) / ".cache";
  }
#endif
  if (base.empty()) {
    std::error_code ec;
    base = std::filesystem::temp_directory_path(ec);
    if (ec) {
      return {};
    }
  }
  return base / "png-analyzer" / "index";
}

std::filesystem::path IndexCache::path_for(const IndexCacheKey& key) const {
  return root_ / ("index-" + key_digest(key) + ".bin");
}

IndexCacheLoadResult IndexCache::load(const IndexCacheKey& key) const {
  IndexCacheLoadResult result;
  if (!valid_key(key)) {
    result.status = IndexCacheLookup::kInvalid;
    result.error = "cache key field is too long";
    return result;
  }

  const std::filesystem::path path = path_for(key);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    if (ec) {
      result.status = IndexCacheLookup::kIoError;
      result.error = "cache existence check failed";
    }
    return result;
  }
  const std::uintmax_t file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    result.status = IndexCacheLookup::kIoError;
    result.error = "cache size query failed";
    return result;
  }
  if (file_size == 0 || file_size > kMaxCacheBytes ||
      file_size > std::numeric_limits<std::size_t>::max()) {
    result.status = IndexCacheLookup::kInvalid;
    result.error = "cache size is invalid";
    return result;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    result.status = IndexCacheLookup::kIoError;
    result.error = "cache open failed";
    return result;
  }
  try {
    std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
      result.status = IndexCacheLookup::kInvalid;
      result.error = "cache read failed";
      return result;
    }
    if (!deserialize(bytes, key, result.data)) {
      result.status = IndexCacheLookup::kInvalid;
      result.error = "cache contents are invalid";
      result.data = {};
      return result;
    }
  } catch (const std::bad_alloc&) {
    result.status = IndexCacheLookup::kInvalid;
    result.error = "cache allocation exceeds the safety limit";
    result.data = {};
    return result;
  }
  result.status = IndexCacheLookup::kHit;
  return result;
}

IndexCacheStoreResult IndexCache::store(const IndexCacheKey& key,
                                        const PersistentIndexData& data) const {
  IndexCacheStoreResult result;
  std::vector<std::byte> bytes;
  if (!serialize(key, data, bytes)) {
    result.error = "index data cannot be serialized safely";
    return result;
  }

  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
  if (ec) {
    result.error = "cache directory creation failed";
    return result;
  }
  const std::filesystem::path path = path_for(key);
  const std::filesystem::path temporary = path.string() + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      result.error = "cache temporary file open failed";
      return result;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
      std::filesystem::remove(temporary, ec);
      result.error = "cache write failed";
      return result;
    }
  }
  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    // POSIX rename replaces an existing file; Windows does not. This fallback
    // only touches the generated cache path and keeps the operation portable.
    std::error_code remove_ec;
    std::filesystem::remove(path, remove_ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
  }
  if (ec) {
    std::filesystem::remove(temporary, ec);
    result.error = "cache publish failed";
    return result;
  }
  result.success = true;
  return result;
}

}  // namespace pnga::deflate_index
