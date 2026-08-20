# Centralized production/test dependency discovery for PNG Analyzer.
#
# Policy (see docs/architecture/REPOSITORY_LAYOUT.md §7, §8 and the project
# bootstrap spec):
#   - libpng, zlib and Catch2 are resolved exclusively through vcpkg manifest
#     mode. Do not fall back to a system copy here.
#   - libpng must be linked only by libs/backend-libpng and explicitly declared
#     oracle tests. This module only provides discovery and the smoke helper.
#   - Catch2 is test-only and must not appear in a production target's
#     transitive link interface.
#
# This file is consumed by the WP-00A `deps-smoke` target (tests/bootstrap)
# and will be reused by wp-001's app/core targets.

find_package(PNG CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

# Captured at include time: CMAKE_CURRENT_LIST_DIR inside a function body refers
# to the caller's directory, so resolve the repository-root cmake/ directory once
# here (this file always lives at <root>/cmake/Dependencies.cmake).
set(PNGA_CMAKE_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# pnga_deliver_dependency_versions(<target>)
#
# Read cmake/dependencies.lock.json and expose each pinned version as a
# compile definition on <target>, e.g.:
#   PNGA_EXPECTED_LIBPNG_VERSION="1.6.58"
#   PNGA_EXPECTED_ZLIB_VERSION="1.3.2"
#   PNGA_EXPECTED_CATCH2_VERSION="3.11.0"
# The runtime smoke test compares these against the macros reported by the
# linked libraries and fails on any mismatch.
function(pnga_deliver_dependency_versions target)
  set(_lock "${PNGA_CMAKE_MODULE_DIR}/dependencies.lock.json")
  if(NOT EXISTS "${_lock}")
    message(FATAL_ERROR
      "pnga_deliver_dependency_versions: no dependencies lock at ${_lock}")
  endif()

  file(READ "${_lock}" _lock_json)
  string(JSON _libpng GET "${_lock_json}" versions libpng)
  string(JSON _zlib   GET "${_lock_json}" versions zlib)
  string(JSON _catch2 GET "${_lock_json}" versions catch2)

  target_compile_definitions("${target}" PRIVATE
    PNGA_EXPECTED_LIBPNG_VERSION="${_libpng}"
    PNGA_EXPECTED_ZLIB_VERSION="${_zlib}"
    PNGA_EXPECTED_CATCH2_VERSION="${_catch2}"
  )
endfunction()