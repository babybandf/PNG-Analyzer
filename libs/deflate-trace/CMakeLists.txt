# libs/deflate-trace — deep Deflate trace (REPOSITORY_LAYOUT.md §5.7).
# WP-500: transparent zlib wrapper trace. Consumes a generic byte stream and
# produces trace-model field nodes with bit spans. ADR-0003: no Qt. The module
# itself is zlib-free; Adler/deflate verification is cross-checked by callers
# with standard zlib (layout §7 dependency table).

add_library(pnga_deflate_trace
  src/token_decoder.cpp
  src/zlib_wrapper.cpp
)
add_library(pnga::deflate_trace ALIAS pnga_deflate_trace)

target_include_directories(pnga_deflate_trace PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_deflate_trace PUBLIC
  pnga::core
  pnga::io
  pnga::trace_model
)
target_compile_features(pnga_deflate_trace PUBLIC cxx_std_20)
set_target_properties(pnga_deflate_trace PROPERTIES POSITION_INDEPENDENT_CODE ON)
