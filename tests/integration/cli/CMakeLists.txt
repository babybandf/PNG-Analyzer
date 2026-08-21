# WP-103 CLI golden tests: exercise the real pnga binary end to end and compare
# deterministic JSON output. The binary path is injected at build time.

add_executable(pnga_cli_tests
  cli_test.cpp
)
target_link_libraries(pnga_cli_tests PRIVATE
  pnga::io
  pnga::png_format
  Catch2::Catch2WithMain
)
target_compile_definitions(pnga_cli_tests PRIVATE
  PNGA_CLI_PATH="$<TARGET_FILE:pnga_cli>"
)
target_compile_features(pnga_cli_tests PRIVATE cxx_std_20)
add_dependencies(pnga_cli_tests pnga_cli)

add_test(NAME cli_inspect_validate_tests COMMAND pnga_cli_tests)
