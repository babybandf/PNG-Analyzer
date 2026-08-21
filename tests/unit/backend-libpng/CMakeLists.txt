# Unit tests for libs/backend-libpng. Encodes fixtures with the public libpng
# write API (test-oracle use of PNG::PNG is permitted by layout §8.2) and
# decodes them through the Reference Backend.

add_executable(pnga_backend_libpng_tests
  reference_test.cpp
)
target_link_libraries(pnga_backend_libpng_tests PRIVATE
  pnga::backend_libpng
  pnga::io
  Catch2::Catch2WithMain
)
target_compile_features(pnga_backend_libpng_tests PRIVATE cxx_std_20)

add_test(NAME backend_libpng_reference_tests COMMAND pnga_backend_libpng_tests)
