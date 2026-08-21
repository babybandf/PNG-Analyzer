# libs/deflate-index — WP-401 fast Deflate block index (REPOSITORY_LAYOUT.md
# §5.6). Consumes a generic byte stream (callers adapt the virtual IDAT stream)
# and never assumes IDAT data is one contiguous file range. ADR-0003: no Qt.

add_library(pnga_deflate_index
  src/access_points.cpp
  src/block_index.cpp
)
add_library(pnga::deflate_index ALIAS pnga_deflate_index)

target_include_directories(pnga_deflate_index PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_deflate_index PUBLIC
  pnga::core
  pnga::io
  ZLIB::ZLIB
)
target_compile_features(pnga_deflate_index PUBLIC cxx_std_20)
set_target_properties(pnga_deflate_index PROPERTIES POSITION_INDEPENDENT_CODE ON)
