# Unit tests for libs/png-reconstruction. Catch2 is test-only (layout §8.4).
# PNG::PNG + ZLIB::ZLIB here are for the WP-303 explicit oracle test only
# (AGENTS.md: libpng may be linked by explicit oracle tests); the production
# pnga_png_reconstruction target itself never links libpng.

add_executable(pnga_png_reconstruction_tests
  native_samples_test.cpp
  pass_reconstruction_test.cpp
  reverse_filter_test.cpp
  scanline_layout_test.cpp
)
target_link_libraries(pnga_png_reconstruction_tests PRIVATE
  pnga::png_reconstruction
  pnga::io
  PNG::PNG
  ZLIB::ZLIB
  Catch2::Catch2WithMain
)
target_compile_features(pnga_png_reconstruction_tests PRIVATE cxx_std_20)

add_test(NAME png_reconstruction_scanline_layout_tests COMMAND pnga_png_reconstruction_tests)
