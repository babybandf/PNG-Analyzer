# Unit tests for libs/deflate-trace. ZLIB/deflate-runtime are linked here for
# WP-500/501/502 cross-validation against standard zlib.

add_executable(pnga_deflate_trace_tests
  token_decoder_test.cpp
  zlib_wrapper_test.cpp
)
target_link_libraries(pnga_deflate_trace_tests PRIVATE
  pnga::deflate_trace
  pnga::deflate_runtime
  pnga::io
  ZLIB::ZLIB
  Catch2::Catch2WithMain
)
target_compile_features(pnga_deflate_trace_tests PRIVATE cxx_std_20)

add_test(NAME deflate_trace_zlib_wrapper_tests COMMAND pnga_deflate_trace_tests)
