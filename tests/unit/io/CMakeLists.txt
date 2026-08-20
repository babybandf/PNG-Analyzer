# Unit tests for libs/io. Catch2 is test-only (layout contract §8.4).

add_executable(pnga_io_tests
  byte_source_test.cpp
)
target_link_libraries(pnga_io_tests PRIVATE pnga::io Catch2::Catch2WithMain)
target_compile_features(pnga_io_tests PRIVATE cxx_std_20)

add_test(NAME io_byte_source_tests COMMAND pnga_io_tests)
