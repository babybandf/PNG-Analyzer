// WP-100 ByteSource implementations: MemoryByteSource and MappedFileByteSource.
//
// Every range check uses the invariant form
//   length <= size && offset <= size - length
// so no intermediate addition can overflow, even for 64-bit offsets near the
// top of the address space.

#include "pnga/io/byte_source.h"

#include <cstring>
#include <limits>

#include <cerrno>
#include <fcntl.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pnga::io {

namespace {

// True when [offset, offset + length) lies within [0, size). No overflow.
bool in_bounds(std::uint64_t offset, std::size_t length,
               std::uint64_t size) noexcept {
  const std::uint64_t len = static_cast<std::uint64_t>(length);
  if (len > size) {
    return false;
  }
  return offset <= size - len;
}

// Pointer arithmetic on a mapped base is defined only for offsets within the
// mapping, which we have already bounds-checked; additionally require the
// offset to fit ptrdiff_t so data() + offset is well-formed on every target.
bool offset_fits_pointer(std::uint64_t offset) noexcept {
  return offset <= static_cast<std::uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
}

}  // namespace

// ---------------------------------------------------------------------------
// MemoryByteSource
// ---------------------------------------------------------------------------

std::uint64_t MemoryByteSource::size() const noexcept {
  return static_cast<std::uint64_t>(data_.size());
}

bool MemoryByteSource::read(std::uint64_t offset, std::byte* out,
                            std::size_t length) const noexcept {
  if (out == nullptr && length != 0) {
    return false;
  }
  if (!in_bounds(offset, length, size())) {
    return false;
  }
  if (length != 0) {
    std::memcpy(out, data_.data() + offset, length);
  }
  return true;
}

std::optional<ByteView> MemoryByteSource::view(
    std::uint64_t offset, std::size_t length) const noexcept {
  if (!in_bounds(offset, length, size())) {
    return std::nullopt;
  }
  const std::byte* base =
      data_.empty() ? nullptr : data_.data() + offset;
  return ByteView{base, length};
}

// ---------------------------------------------------------------------------
// MappedFileByteSource
// ---------------------------------------------------------------------------

MappedFileByteSource::~MappedFileByteSource() {
  close();
}

std::uint64_t MappedFileByteSource::size() const noexcept {
  return size_;
}

bool MappedFileByteSource::read(std::uint64_t offset, std::byte* out,
                                std::size_t length) const noexcept {
  if (out == nullptr && length != 0) {
    return false;
  }
  auto v = view(offset, length);
  if (!v.has_value()) {
    return false;
  }
  if (length != 0) {
    std::memcpy(out, v->data, length);
  }
  return true;
}

std::optional<ByteView> MappedFileByteSource::view(
    std::uint64_t offset, std::size_t length) const noexcept {
  if (!in_bounds(offset, length, size_)) {
    return std::nullopt;
  }
  if (length == 0) {
    // Empty view at a valid offset; data may be null only when the file is
    // empty and offset == 0.
    return ByteView{map_ != nullptr ? static_cast<const std::byte*>(map_) + offset : nullptr, 0};
  }
  if (map_ == nullptr || !offset_fits_pointer(offset)) {
    return std::nullopt;
  }
  return ByteView{static_cast<const std::byte*>(map_) + offset, length};
}

std::error_code MappedFileByteSource::open(
    const std::filesystem::path& path) noexcept {
  close();

#ifdef _WIN32
  const HANDLE file = CreateFileW(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return std::error_code(static_cast<int>(GetLastError()),
                           std::system_category());
  }
  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file, &file_size)) {
    const int err = static_cast<int>(GetLastError());
    CloseHandle(file);
    return std::error_code(err, std::system_category());
  }
  const auto len = static_cast<std::uint64_t>(file_size.QuadPart);
  if (len == 0) {
    CloseHandle(file);
    size_ = 0;
    return std::error_code();
  }
  const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (mapping == nullptr) {
    const int err = static_cast<int>(GetLastError());
    CloseHandle(file);
    return std::error_code(err, std::system_category());
  }
  void* base = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  const int err = base == nullptr ? static_cast<int>(GetLastError()) : 0;
  CloseHandle(mapping);
  CloseHandle(file);
  if (base == nullptr) {
    return std::error_code(err, std::system_category());
  }
  map_ = base;
  size_ = len;
  return std::error_code();
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return std::error_code(errno, std::generic_category());
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0) {
    const int err = errno;
    ::close(fd);
    return std::error_code(err, std::generic_category());
  }
  const auto len = static_cast<std::uint64_t>(st.st_size);
  if (len == 0) {
    ::close(fd);
    size_ = 0;
    return std::error_code();
  }
  void* base = ::mmap(nullptr, static_cast<std::size_t>(len), PROT_READ,
                      MAP_PRIVATE, fd, 0);
  const int err = base == MAP_FAILED ? errno : 0;
  ::close(fd);
  if (base == MAP_FAILED) {
    return std::error_code(err, std::generic_category());
  }
  map_ = base;
  size_ = len;
  return std::error_code();
#endif
}

void MappedFileByteSource::close() noexcept {
  if (map_ == nullptr) {
    return;
  }
#ifdef _WIN32
  ::UnmapViewOfFile(map_);
#else
  ::munmap(map_, static_cast<std::size_t>(size_));
#endif
  map_ = nullptr;
  size_ = 0;
}

std::error_code open_mapped_file(const std::filesystem::path& path,
                                 std::unique_ptr<IByteSource>& out) {
  auto source = std::make_unique<MappedFileByteSource>();
  const std::error_code ec = source->open(path);
  if (ec) {
    out.reset();
    return ec;
  }
  out = std::move(source);
  return std::error_code();
}

}  // namespace pnga::io
