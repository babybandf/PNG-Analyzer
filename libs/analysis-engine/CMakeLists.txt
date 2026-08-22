# libs/analysis-engine — orchestration and artifact caching (REPOSITORY_LAYOUT.md
# §5.10, ADR-0006). WP-203 provides the budgeted ArtifactStore. No Qt (ADR-0003).

add_library(pnga_analysis_engine
  src/artifact_store.cpp
  src/coordinate_query.cpp
  src/filtered_scanlines.cpp
  src/job_scheduler.cpp
  src/pixel_provenance.cpp
  src/query_coordinator.cpp
  src/reconstruct_view_model.cpp
  src/reference_decode.cpp
  src/scanline_anchor.cpp
  src/stage_analysis.cpp
  src/stage_viewport.cpp
  src/trace_query.cpp
  src/trace_orchestrator.cpp
)
add_library(pnga::analysis_engine ALIAS pnga_analysis_engine)

target_include_directories(pnga_analysis_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_analysis_engine PUBLIC
  pnga::trace_model
  pnga::backend_libpng
  pnga::png_format
  pnga::png_reconstruction
  pnga::deflate_runtime
  pnga::deflate_index
  pnga::deflate_trace
  pnga::io
)
target_compile_features(pnga_analysis_engine PUBLIC cxx_std_20)
set_target_properties(pnga_analysis_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
