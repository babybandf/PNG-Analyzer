# libs/backend-libpng — libpng Reference Backend (REPOSITORY_LAYOUT.md §5.9,
# ADR-0002/0008). The ONLY production target that may link PNG::PNG. Uses
# public libpng read APIs only; no private struct access, no forks.
# ADR-0003: this target must never link Qt.

add_library(pnga_backend_libpng
  src/reference.cpp
)
add_library(pnga::backend_libpng ALIAS pnga_backend_libpng)

target_include_directories(pnga_backend_libpng PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_backend_libpng PUBLIC
  PNG::PNG
  ZLIB::ZLIB
  pnga::io
)
target_compile_features(pnga_backend_libpng PUBLIC cxx_std_20)
set_target_properties(pnga_backend_libpng PROPERTIES POSITION_INDEPENDENT_CODE ON)
