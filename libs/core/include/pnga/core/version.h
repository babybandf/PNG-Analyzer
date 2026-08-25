#ifndef PNGA_CORE_VERSION_H
#define PNGA_CORE_VERSION_H

// Single version source is the CMake project version. The version macros live
// in the build-tree generated header so the released binary always matches the
// release tag (layout contract §5.1, §6: generated headers stay out of
// include/).
#include "pnga/core/generated_version.h"

namespace pnga {

// Returns the analyzer version as a static, null-terminated string.
const char* version_string();

}  // namespace pnga

#endif  // PNGA_CORE_VERSION_H
