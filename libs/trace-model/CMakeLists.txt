# libs/trace-model — backend-neutral analysis data model (REPOSITORY_LAYOUT.md
# §5.4, ADR-0004). WP-200: SemanticNode, Selection, spans and coordinates.
# ADR-0003: this target must never link Qt.

add_library(pnga_trace_model
  src/selection.cpp
  src/stage_artifact.cpp
)
add_library(pnga::trace_model ALIAS pnga_trace_model)

target_include_directories(pnga_trace_model PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(pnga_trace_model PUBLIC cxx_std_20)
set_target_properties(pnga_trace_model PROPERTIES POSITION_INDEPENDENT_CODE ON)
