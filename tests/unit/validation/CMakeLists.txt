# Unit tests for libs/validation. Catch2 is test-only (layout contract §8.4).

add_executable(pnga_validation_tests
  integrity_test.cpp
  semantic_resource_decode_test.cpp
  structural_test.cpp
)
target_link_libraries(pnga_validation_tests PRIVATE
  pnga::validation
  pnga::png_format
  pnga::io
  Catch2::Catch2WithMain
)
target_compile_features(pnga_validation_tests PRIVATE cxx_std_20)

add_test(NAME validation_structural_tests COMMAND pnga_validation_tests)
