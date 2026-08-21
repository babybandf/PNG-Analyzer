# Unit tests for libs/png-format. Catch2 is test-only (layout contract §8.4).

add_executable(pnga_png_format_tests
  chunk_index_test.cpp
  virtual_idat_stream_test.cpp
)
target_link_libraries(pnga_png_format_tests PRIVATE pnga::png_format Catch2::Catch2WithMain)
target_compile_features(pnga_png_format_tests PRIVATE cxx_std_20)

add_test(NAME png_format_chunk_index_tests COMMAND pnga_png_format_tests)
