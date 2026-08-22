# WP-604A: deterministic in-memory performance corpus and runner. The runner
# records measurements but deliberately does not enforce thresholds; that is
# the scope of WP-604B.

add_executable(pnga_performance_runner
  performance_runner.cpp
)
target_link_libraries(pnga_performance_runner PRIVATE
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  Catch2::Catch2
)
target_compile_features(pnga_performance_runner PRIVATE cxx_std_20)
target_include_directories(pnga_performance_runner PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

add_test(NAME performance_corpus_runner COMMAND pnga_performance_runner)
set_tests_properties(performance_corpus_runner PROPERTIES
  LABELS "performance;corpus"
)
