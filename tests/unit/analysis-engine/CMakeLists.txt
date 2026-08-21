# Unit tests for libs/analysis-engine. Catch2 is test-only (layout §8.4).

add_executable(pnga_analysis_engine_tests
  artifact_store_test.cpp
)
target_link_libraries(pnga_analysis_engine_tests PRIVATE
  pnga::analysis_engine
  pnga::trace_model
  Catch2::Catch2WithMain
)
target_compile_features(pnga_analysis_engine_tests PRIVATE cxx_std_20)

add_test(NAME analysis_engine_artifact_store_tests COMMAND pnga_analysis_engine_tests)
