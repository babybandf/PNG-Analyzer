# libs/deflate-runtime — fast production Inflate on public zlib APIs
# (REPOSITORY_LAYOUT.md §5.5). WP-301: streaming inflate wrapper with bounded
# output and Adler-32 / truncation reporting. No Qt (ADR-0003).

add_library(pnga_deflate_runtime
  src/inflate.cpp
)
add_library(pnga::deflate_runtime ALIAS pnga_deflate_runtime)

target_include_directories(pnga_deflate_runtime PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_deflate_runtime PUBLIC
  ZLIB::ZLIB
  pnga::io
)
target_compile_features(pnga_deflate_runtime PUBLIC cxx_std_20)
set_target_properties(pnga_deflate_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
