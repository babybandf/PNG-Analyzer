# apps/pnga-cli — CLI entry point (REPOSITORY_LAYOUT.md §4). Composition only:
# it links libs/core public headers and must never link Qt (ADR-0003).

add_executable(pnga_cli
  src/main.cpp
)
set_target_properties(pnga_cli PROPERTIES OUTPUT_NAME pnga)
target_link_libraries(pnga_cli PRIVATE pnga::core)
target_compile_features(pnga_cli PRIVATE cxx_std_20)