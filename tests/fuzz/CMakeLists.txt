# WP-603A deterministic parser/stream fuzz smoke. This is a bounded Catch2
# harness for every preset; a future sanitizer job may wrap the same target in
# a native coverage-guided engine without changing production code.

add_executable(pnga_fuzz_smoke_tests
  decode_reconstruction_fuzz_test.cpp
  parser_stream_fuzz_test.cpp
)
target_link_libraries(pnga_fuzz_smoke_tests PRIVATE
  pnga::analysis_engine
  pnga::deflate_trace
  pnga::png_format
  pnga::io
  Catch2::Catch2WithMain
)
target_compile_features(pnga_fuzz_smoke_tests PRIVATE cxx_std_20)
target_include_directories(pnga_fuzz_smoke_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

add_test(NAME fuzz_parser_stream_smoke_tests COMMAND pnga_fuzz_smoke_tests)
