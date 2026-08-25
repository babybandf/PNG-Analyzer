# libs/core — small, domain-independent primitives (REPOSITORY_LAYOUT.md §5.1).
# ADR-0003: this target must never link Qt.

# The version macros (PNGA_VERSION_*) are generated from the CMake project
# version, which is the single source for the binary, About dialog, CLI and
# package names. Release builds override it with the git tag version.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/generated_version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/pnga/core/generated_version.h
  @ONLY
)

add_library(pnga_core
  src/version.cpp
)
add_library(pnga::core ALIAS pnga_core)

target_include_directories(pnga_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
)
target_compile_features(pnga_core PUBLIC cxx_std_20)
set_target_properties(pnga_core PROPERTIES POSITION_INDEPENDENT_CODE ON)