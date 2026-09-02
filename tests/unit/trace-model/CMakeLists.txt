# Unit tests for libs/trace-model. Catch2 is test-only (layout contract §8.4).

add_executable(pnga_trace_model_tests
  offset_range_test.cpp
  compression_navigation_test.cpp
  selection_test.cpp
)
target_link_libraries(pnga_trace_model_tests PRIVATE
  pnga::trace_model
  Catch2::Catch2WithMain
)
target_compile_features(pnga_trace_model_tests PRIVATE cxx_std_20)

add_test(NAME trace_model_selection_tests COMMAND pnga_trace_model_tests)
