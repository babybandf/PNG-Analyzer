// pnga — command-line entry point (WP-001 walking skeleton).
//
// Only prints product identity for now. The inspect/validate commands arrive
// with WP-103, on top of the WP-100/101 parser slices.

#include <pnga/core/version.h>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (std::strcmp(a, "--version") == 0 || std::strcmp(a, "-V") == 0) {
      std::printf("pnga %s\n", pnga::version_string());
      return 0;
    }
    std::fprintf(stderr, "pnga: unknown argument '%s'\n", a);
    return 2;
  }
  std::printf("pnga: PNG Analyzer CLI (walking skeleton). Try --version.\n");
  return 0;
}