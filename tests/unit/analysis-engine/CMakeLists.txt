# Unit tests for libs/analysis-engine. Catch2 is test-only (layout §8.4).

add_executable(pnga_analysis_engine_tests
  artifact_store_test.cpp
  block_inspector_test.cpp
  coordinate_query_test.cpp
  decode_trace_inspector_test.cpp
  filtered_scanlines_test.cpp
  huffman_inspector_test.cpp
  job_scheduler_test.cpp
  pixel_provenance_test.cpp
  query_coordinator_test.cpp
  reconstruct_view_model_test.cpp
  scanline_anchor_test.cpp
  stage_analysis_test.cpp
  stage_viewport_test.cpp
  trace_query_test.cpp
  trace_orchestrator_test.cpp
  trace_inspector_bundle_test.cpp
  trace_inspector_state_test.cpp
  statistics_adapter_test.cpp
)
target_link_libraries(pnga_analysis_engine_tests PRIVATE
  pnga::analysis_engine
  pnga::trace_model
  pnga::png_format
  pnga::png_reconstruction
  pnga::io
  PNG::PNG
  ZLIB::ZLIB
  Catch2::Catch2WithMain
)
target_include_directories(pnga_analysis_engine_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)
target_compile_features(pnga_analysis_engine_tests PRIVATE cxx_std_20)

add_test(NAME analysis_engine_artifact_store_tests COMMAND pnga_analysis_engine_tests)
