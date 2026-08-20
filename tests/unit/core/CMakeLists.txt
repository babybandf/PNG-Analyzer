# Unit tests for libs/core. Catch2 is a test-only dependency (layout contract
# §8.4) and is consumed via the vcpkg-managed Catch2::Catch2WithMain.

add_executable(pnga_core_tests
  core_version_test.cpp
)
target_link_libraries(pnga_core_tests PRIVATE pnga::core Catch2::Catch2WithMain)
target_compile_features(pnga_core_tests PRIVATE cxx_std_20)

# Register the whole Catch2 suite as one ctest test. (vcpkg's catch2 installs
# a newer discovery module than upstream's Catch2.cmake, so catch_discover_tests
# is not guaranteed; a single add_test is version-agnostic and deterministic.)
add_test(NAME core_version_tests COMMAND pnga_core_tests)