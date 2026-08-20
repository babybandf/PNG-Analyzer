#ifndef PNGA_IO_BYTE_SOURCE_H
#define PNGA_IO_BYTE_SOURCE_H

// WP-100: safe, zero-copy, random-access input abstraction for the parser
// (REPOSITORY_LAYOUT.md §5.2, ADR-0003/0005).
//
// Lifetime contract: a ByteView is a borrowed window into storage owned by an
// IByteSource. It is valid only while the owning source is alive and must not
// outlive it. read() copies and is safe to use with any caller buffer.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace pnga::io {

// Immutable window into bytes owned by a ByteSource. Copying the struct does
// not extend the underlying storage lifetime.
struct ByteView {
  const std::byte* data = nullptr;
  std::size_t size = 0;

  bool empty() const noexcept { return size == 0; }
  bool valid() const noexcept { return data != nullptr; }
};

// Read-only random-access byte source. Implementations must be safe for
// concurrent read()/view() calls from multiple threads; view() results are
// immutable and borrowed, read() never aliases caller storage.
//
// All offset/length arithmetic is checked: no implementation may read or
// return bytes outside [0, size()). A request that would overflow or fall out
// of range fails cleanly (false / nullopt) instead of wrapping.
class IByteSource {
 public:
  virtual ~IByteSource() = default;

  // Total length in bytes. 64-bit so files at or above 4 GiB are not
  // truncated; callers must not narrow this to 32 bits.
  virtual std::uint64_t size() const noexcept = 0;

  // Copies `length` bytes starting at `offset` into `out`, which must be able
  // to hold `length` bytes. Returns false without touching `out` when the
  // range lies outside the source. `length == 0` with `offset <= size()` is
  // valid and copies nothing.
  virtual bool read(std::uint64_t offset, std::byte* out,
                    std::size_t length) const noexcept = 0;

  // Zero-copy view of [offset, offset + length). Returns nullopt when the
  // range lies outside the source or cannot be mapped. The returned ByteView
  // is borrowed and valid only until the source is destroyed.
  virtual std::optional<ByteView> view(std::uint64_t offset,
                                       std::size_t length) const noexcept = 0;
};

// ByteSource backed by an owned in-memory copy. The source does not alias the
// caller's buffer; mutations to that buffer after construction are not
// observed. Used for tests, small payloads and windowed reads.
class MemoryByteSource final : public IByteSource {
 public:
  explicit MemoryByteSource(std::vector<std::byte> data) noexcept
      : data_(std::move(data)) {}

  std::uint64_t size() const noexcept override;
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override;
  std::optional<ByteView> view(std::uint64_t offset,
                               std::size_t length) const noexcept override;

 private:
  std::vector<std::byte> data_;
};

// ByteSource backed by a memory-mapped regular file (mmap on POSIX,
// MapViewOfFile on Windows). Views remain valid for the source's lifetime and
// share no copies with the file. Opening a file does not block on I/O beyond
// the kernel's mapping setup.
class MappedFileByteSource final : public IByteSource {
 public:
  friend std::error_code open_mapped_file(const std::filesystem::path&,
                                          std::unique_ptr<IByteSource>&);

  MappedFileByteSource() noexcept = default;
  ~MappedFileByteSource() override;

  MappedFileByteSource(const MappedFileByteSource&) = delete;
  MappedFileByteSource& operator=(const MappedFileByteSource&) = delete;

  std::uint64_t size() const noexcept override;
  bool read(std::uint64_t offset, std::byte* out,
            std::size_t length) const noexcept override;
  std::optional<ByteView> view(std::uint64_t offset,
                               std::size_t length) const noexcept override;

 private:
  // Returns std::error_code() on success. Internal; use open_mapped_file().
  std::error_code open(const std::filesystem::path& path) noexcept;
  void close() noexcept;

  void* map_ = nullptr;          // mapped base, nullptr when unmapped
  std::uint64_t size_ = 0;
};

// Opens `path` read-only and maps it. On success `out` owns a valid source;
// on failure `out` is reset to nullptr and a nonzero error_code is returned.
std::error_code open_mapped_file(const std::filesystem::path& path,
                                 std::unique_ptr<IByteSource>& out);

}  // namespace pnga::io

#endif  // PNGA_IO_BYTE_SOURCE_H
