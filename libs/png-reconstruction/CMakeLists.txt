# libs/png-reconstruction — deterministic PNG reconstruction after Inflate
# (REPOSITORY_LAYOUT.md §5.8). WP-300: safe scanline layout geometry and
# WP-302: reverse filters.
# Testable without Qt and without libpng. ADR-0003: never link Qt.

add_library(pnga_png_reconstruction
  src/reverse_filter.cpp
  src/scanline_layout.cpp
)
add_library(pnga::png_reconstruction ALIAS pnga_png_reconstruction)

target_include_directories(pnga_png_reconstruction PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(pnga_png_reconstruction PUBLIC cxx_std_20)
set_target_properties(pnga_png_reconstruction PROPERTIES POSITION_INDEPENDENT_CODE ON)
