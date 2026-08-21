// pnga — PNG Analyzer command-line entry point.
//
// WP-001: version. WP-103: `inspect` and `validate` with deterministic JSON
// output. CLI composition only: it uses core public headers plus the parser
// libraries directly until the analysis engine (M2) provides orchestration.

#include <pnga/core/version.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>

#include "report.h"

namespace {

// Fixed exit codes (WP-103): distinct values for success, read failure, format
// error and validation issues.
constexpr int kExitOk = 0;
constexpr int kExitIoError = 1;
constexpr int kExitFormatError = 2;
constexpr int kExitValidationIssues = 3;

// Format errors stop scanning; trailing-bytes-after-IEND is a validation
// issue that still yields a parseable tree.
int exit_code_for(const pnga::png_format::ChunkIndex& index) {
  using pnga::png_format::ChunkIssueKind;
  bool validation = false;
  for (const auto& issue : index.issues) {
    switch (issue.kind) {
      case ChunkIssueKind::kBadSignature:
      case ChunkIssueKind::kTruncatedSignature:
      case ChunkIssueKind::kTruncatedHeader:
      case ChunkIssueKind::kTruncatedData:
      case ChunkIssueKind::kTruncatedCrc:
        return kExitFormatError;
      case ChunkIssueKind::kTrailingBytesAfterIend:
        validation = true;
        break;
    }
  }
  return validation ? kExitValidationIssues : kExitOk;
}

void print_usage(FILE* out) {
  std::fprintf(out,
               "pnga - PNG Analyzer CLI\n"
               "usage:\n"
               "  pnga --version           print version and exit\n"
               "  pnga inspect <file>      dump the physical Chunk tree\n"
               "  pnga validate <file>     report structural issues\n"
               "options:\n"
               "  --json                   deterministic JSON output\n"
               "exit codes:\n"
               "  0 ok; 1 cannot read file; 2 format error; 3 validation issues\n");
}

bool has_flag(int argc, char** argv, const char* flag) {
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) {
      return true;
    }
  }
  return false;
}

// Returns the positional file argument, or nullptr when absent.
const char* file_argument(int argc, char** argv) {
  for (int i = 2; i < argc; ++i) {
    if (std::strcmp(argv[i], "--json") != 0 && argv[i][0] != '-') {
      return argv[i];
    }
  }
  return nullptr;
}

int run_analyze_command(int argc, char** argv, bool validate) {
  const char* file = file_argument(argc, argv);
  if (file == nullptr) {
    std::fprintf(stderr, "pnga: missing file argument\n");
    print_usage(stderr);
    return kExitFormatError;
  }

  std::unique_ptr<pnga::io::IByteSource> source;
  const std::error_code ec =
      pnga::io::open_mapped_file(std::filesystem::path(file), source);
  if (ec) {
    std::puts(pnga::cli::error_json(file, ec.value()).c_str());
    return kExitIoError;
  }

  const pnga::png_format::ChunkIndex index =
      pnga::png_format::index_chunks(*source);

  const bool json = has_flag(argc, argv, "--json");
  if (validate) {
    std::puts(pnga::cli::validate_json(file, index).c_str());
  } else if (json) {
    std::puts(pnga::cli::inspect_json(file, index).c_str());
  } else {
    // Human-readable Chunk tree.
    std::printf("file: %s (%llu bytes) signature=%s\n", file,
                static_cast<unsigned long long>(index.file_size),
                index.valid_signature ? "valid" : "invalid");
    for (const auto& n : index.chunks) {
      std::printf("  %s data=%llu@%llu crc@%llu\n", n.text().c_str(),
                  static_cast<unsigned long long>(n.data_length),
                  static_cast<unsigned long long>(n.data_offset),
                  static_cast<unsigned long long>(n.crc_offset));
    }
    for (const auto& issue : index.issues) {
      std::printf("  issue: %s @ %llu\n",
                  pnga::cli::issue_kind_text(issue.kind),
                  static_cast<unsigned long long>(issue.offset));
    }
  }
  return exit_code_for(index);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage(stderr);
    return kExitFormatError;
  }

  const char* cmd = argv[1];
  if (std::strcmp(cmd, "--version") == 0 || std::strcmp(cmd, "-V") == 0) {
    std::printf("pnga %s\n", pnga::version_string());
    return kExitOk;
  }
  if (std::strcmp(cmd, "--help") == 0 || std::strcmp(cmd, "-h") == 0) {
    print_usage(stdout);
    return kExitOk;
  }
  if (std::strcmp(cmd, "inspect") == 0) {
    return run_analyze_command(argc, argv, /*validate=*/false);
  }
  if (std::strcmp(cmd, "validate") == 0) {
    return run_analyze_command(argc, argv, /*validate=*/true);
  }

  std::fprintf(stderr, "pnga: unknown command '%s'\n", cmd);
  print_usage(stderr);
  return kExitFormatError;
}
