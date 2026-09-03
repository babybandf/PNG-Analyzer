# WP-607C controlled corpus (test-only, Qt-free). The fixture library is
# independent of production parsers/decoders; generated corpus output is
# materialized only in the build tree (never under this source directory).

add_library(pnga_test_wp607c_corpus STATIC controlled_fixture.cpp)
target_include_directories(pnga_test_wp607c_corpus PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnga_test_wp607c_corpus PUBLIC ZLIB::ZLIB)
target_compile_features(pnga_test_wp607c_corpus PUBLIC cxx_std_20)

add_executable(pnga_wp607c_png_facts_tests png_facts_test.cpp)
target_link_libraries(pnga_wp607c_png_facts_tests PRIVATE
  pnga_test_wp607c_corpus
  pnga::png_format pnga::validation pnga::png_reconstruction pnga::io
  PNG::PNG ZLIB::ZLIB Catch2::Catch2WithMain)
add_test(NAME wp607c_png_facts_tests COMMAND pnga_wp607c_png_facts_tests)
set_tests_properties(wp607c_png_facts_tests PROPERTIES
  LABELS "corpus;wp607c")

add_executable(pnga_wp607c_trace_facts_tests trace_facts_test.cpp)
target_link_libraries(pnga_wp607c_trace_facts_tests PRIVATE
  pnga_test_wp607c_corpus
  pnga::deflate_index pnga::deflate_trace pnga::png_format pnga::io
  ZLIB::ZLIB Catch2::Catch2WithMain)
add_test(NAME wp607c_trace_facts_tests COMMAND pnga_wp607c_trace_facts_tests)
set_tests_properties(wp607c_trace_facts_tests PROPERTIES
  LABELS "corpus;wp607c")
