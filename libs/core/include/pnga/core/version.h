#ifndef PNGA_CORE_VERSION_H
#define PNGA_CORE_VERSION_H

// Single version source shared by CLI and GUI (layout contract §5.1: build
// version information lives in libs/core). project() derives the CMake version
// from these; keep the two in sync.
#define PNGA_VERSION_MAJOR 0
#define PNGA_VERSION_MINOR 1
#define PNGA_VERSION_PATCH 0
#define PNGA_VERSION_STR "0.1.0"

namespace pnga {

// Returns the analyzer version as a static, null-terminated string.
const char* version_string();

}  // namespace pnga

#endif  // PNGA_CORE_VERSION_H