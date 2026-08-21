# Unit tests for libs/deflate-index.

add_executable(pnga_deflate_index_tests
  access_points_test.cpp
  block_index_test.cpp
  session_index_test.cpp
)
target_link_libraries(pnga_deflate_index_tests PRIVATE
  pnga::deflate_index
  pnga::png_format
  pnga::io
  ZLIB::ZLIB
  Catch2::Catch2WithMain
)
target_compile_features(pnga_deflate_index_tests PRIVATE cxx_std_20)

add_test(NAME deflate_index_block_index_tests COMMAND pnga_deflate_index_tests)
