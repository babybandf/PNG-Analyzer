# WP-607C controlled corpus (test-only, Qt-free). The fixture library is
# independent of production parsers/decoders; generated corpus output is
# materialized only in the build tree (never under this source directory).

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# --- one aggregate corpus revision (ruling R1) --------------------------------
#
# The revision covers exactly FOUR repository files, hashed in this order:
# manifest.yaml, controlled_fixture.h, controlled_fixture.cpp and
# generate_controlled_corpus.cpp. Each file contributes its raw 32-byte
# SHA-256 digest; the digests are concatenated as raw bytes and hashed once
# more, matching scripts/verify_wp607c_manifest.py aggregate_revision exactly.
# string(SHA256) cannot carry the NUL bytes a raw digest may contain, so the
# raw concatenation is materialized and hashed with file(SHA256); the
# configure-time equivalence assertion below proves byte-identical agreement
# with the Python validator on every configure.
set(WP607C_REVISION_INPUTS
  manifest.yaml
  controlled_fixture.h
  controlled_fixture.cpp
  generate_controlled_corpus.cpp
)
foreach(name ${WP607C_REVISION_INPUTS})
  list(APPEND WP607C_REVISION_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY
  CMAKE_CONFIGURE_DEPENDS ${WP607C_REVISION_PATHS})

set(WP607C_REVISION_HEX "")
foreach(path ${WP607C_REVISION_PATHS})
  file(SHA256 "${path}" component)
  string(APPEND WP607C_REVISION_HEX "${component}")
endforeach()
set(WP607C_REVISION_BLOB "${CMAKE_CURRENT_BINARY_DIR}/wp607c-revision.blob")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c
          "import sys; open(sys.argv[1], 'wb').write(bytes.fromhex(sys.argv[2]))"
          "${WP607C_REVISION_BLOB}" "${WP607C_REVISION_HEX}"
  RESULT_VARIABLE WP607C_BLOB_RESULT)
if(NOT WP607C_BLOB_RESULT EQUAL 0)
  message(FATAL_ERROR "WP-607C: cannot materialize the raw revision digest")
endif()
file(SHA256 "${WP607C_REVISION_BLOB}" WP607C_CORPUS_REVISION)
file(REMOVE "${WP607C_REVISION_BLOB}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/scripts/verify_wp607c_manifest.py"
          --print-revision
  OUTPUT_VARIABLE WP607C_PYTHON_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE WP607C_REVISION_CHECK_RESULT)
if(NOT WP607C_REVISION_CHECK_RESULT EQUAL 0)
  message(FATAL_ERROR "WP-607C: the Python validator cannot compute the "
    "corpus revision")
endif()
if(NOT WP607C_PYTHON_REVISION STREQUAL WP607C_CORPUS_REVISION)
  message(FATAL_ERROR "WP-607C: CMake corpus revision "
    "${WP607C_CORPUS_REVISION} differs from the Python validator revision "
    "${WP607C_PYTHON_REVISION}")
endif()
set(PNGA_WP607C_CORPUS_REVISION "${WP607C_CORPUS_REVISION}"
  CACHE INTERNAL "WP-607C aggregate corpus revision")

# --- fixture library and focused fact tests -----------------------------------

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

# --- generator and CTest setup fixture ----------------------------------------

add_executable(pnga_generate_wp607c_corpus generate_controlled_corpus.cpp)
target_link_libraries(pnga_generate_wp607c_corpus PRIVATE pnga_test_wp607c_corpus)
target_compile_features(pnga_generate_wp607c_corpus PRIVATE cxx_std_20)
target_compile_definitions(pnga_generate_wp607c_corpus PRIVATE
  PNGA_WP607C_CORPUS_REVISION="${PNGA_WP607C_CORPUS_REVISION}"
  PNGA_WP607C_SOURCE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME wp607c_generate_corpus
  COMMAND pnga_generate_wp607c_corpus
          --output ${CMAKE_BINARY_DIR}/tests/corpus/wp-607c)
set_tests_properties(wp607c_generate_corpus PROPERTIES
  FIXTURES_SETUP wp607c-generated-corpus
  LABELS "corpus;wp607c")

# --- manifest contract gate -----------------------------------------------------

add_test(NAME wp607c_manifest_tests
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/scripts/verify_wp607c_manifest.py"
          --manifest "${CMAKE_CURRENT_SOURCE_DIR}/manifest.yaml"
          --catalog "${CMAKE_BINARY_DIR}/tests/corpus/wp-607c/index.json"
          --build-dir "${CMAKE_BINARY_DIR}")
set_tests_properties(wp607c_manifest_tests PROPERTIES
  FIXTURES_REQUIRED wp607c-generated-corpus
  LABELS "corpus;wp607c")
