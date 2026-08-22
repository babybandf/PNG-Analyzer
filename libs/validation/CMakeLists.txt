# libs/validation — structural, integrity, semantic and specification
# validation (REPOSITORY_LAYOUT.md §5.11). WP-102 provides the first structural
# rule batch. ADR-0003: this target must never link Qt.

add_library(pnga_validation
  src/decode.cpp
  src/integrity.cpp
  src/resource.cpp
  src/semantic.cpp
  src/structural.cpp
)
add_library(pnga::validation ALIAS pnga_validation)

target_include_directories(pnga_validation PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_validation PUBLIC pnga::png_format)
target_compile_features(pnga_validation PUBLIC cxx_std_20)
set_target_properties(pnga_validation PROPERTIES POSITION_INDEPENDENT_CODE ON)
