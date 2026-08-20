// WP-00A dependency smoke: verifies that the linked libpng, zlib and Catch2
// report exactly the versions pinned in cmake/dependencies.lock.json.
//
// The expected versions arrive as compile definitions injected by
// pnga_deliver_dependency_versions (see cmake/Dependencies.cmake):
//   PNGA_EXPECTED_LIBPNG_VERSION, PNGA_EXPECTED_ZLIB_VERSION,
//   PNGA_EXPECTED_CATCH2_VERSION
//
// Output is deterministic (no locale/time dependence) and the process exit
// status is nonzero on any mismatch so ctest can fail it.

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <png.h>
#include <zlib.h>
#include <catch2/catch_version_macros.hpp>

#ifndef PNGA_EXPECTED_LIBPNG_VERSION
#define PNGA_EXPECTED_LIBPNG_VERSION "not-defined"
#endif
#ifndef PNGA_EXPECTED_ZLIB_VERSION
#define PNGA_EXPECTED_ZLIB_VERSION "not-defined"
#endif
#ifndef PNGA_EXPECTED_CATCH2_VERSION
#define PNGA_EXPECTED_CATCH2_VERSION "not-defined"
#endif

namespace {

// Returns false (and prints a mismatch line) when a runtime macro does not
// equal the expected value; leaves flushing to the caller so the report stays
// deterministic and single-block.
bool check(const char* name, const char* runtime, const char* expected) {
  const bool ok = std::strcmp(runtime, expected) == 0;
  std::printf("  %-22s runtime=%-12s expected=%-12s %s\n",
              name, runtime, expected, ok ? "OK" : "MISMATCH");
  return ok;
}

}  // namespace

int main() {
  std::printf("pnga deps-smoke version report\n");

  char catch2_version[64] = {0};
  std::snprintf(catch2_version, sizeof(catch2_version), "%u.%u.%u",
                CATCH_VERSION_MAJOR, CATCH_VERSION_MINOR, CATCH_VERSION_PATCH);

  const bool ok = check("libpng", PNG_LIBPNG_VER_STRING,
                        PNGA_EXPECTED_LIBPNG_VERSION) &&
                  check("zlib", ZLIB_VERSION, PNGA_EXPECTED_ZLIB_VERSION) &&
                  check("catch2", catch2_version, PNGA_EXPECTED_CATCH2_VERSION);

  std::printf("deps-smoke %s\n", ok ? "PASS" : "FAIL");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}