# WP-305 Trace vs libpng differential harness (layout contract §11). Test-only.
# PNG::PNG + ZLIB::ZLIB are linked here as the raw libpng oracle (AGENTS.md:
# libpng may be linked by explicit oracle tests); the production targets never
# see them.

add_executable(pnga_differential_tests
  differential_harness.cpp
  differential_harness_test.cpp
)
target_link_libraries(pnga_differential_tests PRIVATE
  pnga::analysis_engine
  pnga::png_format
  pnga::png_reconstruction
  pnga::io
  pnga_test_wp607c_corpus
  PNG::PNG
  ZLIB::ZLIB
  Catch2::Catch2WithMain
)
target_include_directories(pnga_differential_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
target_compile_features(pnga_differential_tests PRIVATE cxx_std_20)

add_test(NAME differential_harness_tests COMMAND pnga_differential_tests)
