# Unit tests for libs/deflate-runtime. Catch2 is test-only (layout §8.4).

add_executable(pnga_deflate_runtime_tests
  inflate_snapshot_test.cpp
  inflate_test.cpp
)
target_link_libraries(pnga_deflate_runtime_tests PRIVATE
  pnga::deflate_runtime
  pnga::io
  ZLIB::ZLIB
  Catch2::Catch2WithMain
)
target_compile_features(pnga_deflate_runtime_tests PRIVATE cxx_std_20)

add_test(NAME deflate_runtime_inflate_tests COMMAND pnga_deflate_runtime_tests)
