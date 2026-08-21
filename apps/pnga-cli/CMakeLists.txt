# apps/pnga-cli — CLI entry point (REPOSITORY_LAYOUT.md §4). Composition only:
# it must never link Qt (ADR-0003). WP-103 composes the parser libraries
# directly; M2's analysis engine will become the single CLI dependency.

add_executable(pnga_cli
  src/main.cpp
  src/report.cpp
)
set_target_properties(pnga_cli PROPERTIES OUTPUT_NAME pnga)
target_link_libraries(pnga_cli PRIVATE
  pnga::core
  pnga::io
  pnga::png_format
)
target_compile_features(pnga_cli PRIVATE cxx_std_20)