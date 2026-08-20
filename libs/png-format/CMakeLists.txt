# libs/png-format — physical and logical PNG structure (REPOSITORY_LAYOUT.md §5.3).
# ADR-0003: this target must never link Qt.

add_library(pnga_png_format
  src/chunk_index.cpp
)
add_library(pnga::png_format ALIAS pnga_png_format)

target_include_directories(pnga_png_format PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_png_format PUBLIC pnga::io)
target_compile_features(pnga_png_format PUBLIC cxx_std_20)
set_target_properties(pnga_png_format PROPERTIES POSITION_INDEPENDENT_CODE ON)
