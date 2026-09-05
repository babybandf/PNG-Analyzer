#ifndef PNGA_CLI_STATISTICS_COMMAND_H
#define PNGA_CLI_STATISTICS_COMMAND_H

// WP-602E: `pnga statistics <file> --format json|csv`. The CLI composes the
// shared analysis engine and the sole Qt-free statistics serializer; it never
// owns schema fields or report formatting. Contract (WP-602E review ruling
// R10): stdout carries only the report bytes, stderr only diagnostics, and
// the exit codes are frozen — 0 ready, 1 I/O, 2 argument/format error,
// 3 validation issues with usable statistics, 4 partial/cancel/budget.

#include <cstdio>
#include <filesystem>

namespace pnga::cli {

enum class StatisticsOutputFormat { kJson, kCsv };

// Statistics-specific usage text: the pnga.statistics schema version and the
// full exit-code table required by the CLI contract.
void print_statistics_usage(FILE* out);

// Runs the `statistics` subcommand for argv[2..]: parses the arguments,
// prints diagnostics on stderr for argument errors (exit 2) and dispatches
// to run_statistics_command.
int run_statistics_command_line(int argc, char** argv, FILE* out, FILE* err);

// Collects whole-document statistics for `file` synchronously and writes the
// serialized report bytes to `out` with fwrite (the serializer already owns
// the final LF). Diagnostics go to `err` only. Returns the frozen exit code.
int run_statistics_command(const std::filesystem::path& file,
                           StatisticsOutputFormat format,
                           FILE* out, FILE* err);

}  // namespace pnga::cli

#endif  // PNGA_CLI_STATISTICS_COMMAND_H
