# WP-604A: deterministic in-memory performance corpus and runner. The runner
# records measurements but deliberately does not enforce thresholds; that is
# the scope of WP-604B. WP-607C: the large scenario comes from the shared
# controlled corpus and the record carries the aggregate corpus revision.

add_executable(pnga_performance_runner
  performance_runner.cpp
)
target_link_libraries(pnga_performance_runner PRIVATE
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  pnga_test_wp607c_corpus
  Catch2::Catch2
)
target_compile_features(pnga_performance_runner PRIVATE cxx_std_20)
target_include_directories(pnga_performance_runner PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
target_compile_definitions(pnga_performance_runner PRIVATE
  PNGA_WP607C_CORPUS_REVISION="${PNGA_WP607C_CORPUS_REVISION}"
)

# Peak-RSS evidence uses GetProcessMemoryInfo; psapi.lib is required when the
# platform headers map it to the legacy psapi.dll entry point (PSAPI_VERSION 1).
if(WIN32)
  target_link_libraries(pnga_performance_runner PRIVATE psapi)
endif()

add_test(NAME performance_corpus_runner COMMAND pnga_performance_runner)

# WP-APNG-INSPECT T11: per-frame inspection performance record (machine-shaped
# JSON; the candidate gate compares it against the T00 baseline).
add_executable(pnga_apng_inspection_perf
  apng_inspection_test.cpp
)
target_link_libraries(pnga_apng_inspection_perf PRIVATE
  pnga::analysis_engine
  pnga::png_format
  pnga::io
)
target_compile_features(pnga_apng_inspection_perf PRIVATE cxx_std_20)
target_include_directories(pnga_apng_inspection_perf PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
set_tests_properties(performance_corpus_runner PROPERTIES
  LABELS "performance;corpus"
)
