# Unit tests for libs/statistics (WP-602A). Catch2 is test-only.

add_executable(pnga_statistics_tests
  statistics_test.cpp
)
target_link_libraries(pnga_statistics_tests PRIVATE
  pnga::statistics
  Catch2::Catch2WithMain
)
target_compile_features(pnga_statistics_tests PRIVATE cxx_std_20)

add_test(NAME statistics_engine_tests COMMAND pnga_statistics_tests)
