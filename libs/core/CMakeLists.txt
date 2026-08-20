# libs/core — small, domain-independent primitives (REPOSITORY_LAYOUT.md §5.1).
# ADR-0003: this target must never link Qt.

add_library(pnga_core
  src/version.cpp
)
add_library(pnga::core ALIAS pnga_core)

target_include_directories(pnga_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(pnga_core PUBLIC cxx_std_20)
set_target_properties(pnga_core PROPERTIES POSITION_INDEPENDENT_CODE ON)