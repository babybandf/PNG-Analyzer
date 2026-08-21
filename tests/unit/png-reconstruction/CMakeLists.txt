# Unit tests for libs/png-reconstruction. Catch2 is test-only (layout §8.4).

add_executable(pnga_png_reconstruction_tests
  reverse_filter_test.cpp
  scanline_layout_test.cpp
)
target_link_libraries(pnga_png_reconstruction_tests PRIVATE
  pnga::png_reconstruction
  Catch2::Catch2WithMain
)
target_compile_features(pnga_png_reconstruction_tests PRIVATE cxx_std_20)

add_test(NAME png_reconstruction_scanline_layout_tests COMMAND pnga_png_reconstruction_tests)
