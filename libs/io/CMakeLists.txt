# libs/io — input access and file identity (REPOSITORY_LAYOUT.md §5.2).
# ADR-0003: this target must never link Qt.

add_library(pnga_io
  src/byte_source.cpp
)
add_library(pnga::io ALIAS pnga_io)

target_include_directories(pnga_io PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(pnga_io PUBLIC cxx_std_20)
set_target_properties(pnga_io PROPERTIES POSITION_INDEPENDENT_CODE ON)
