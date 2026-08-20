# WP-00A dependency smoke test (tests/bootstrap).

# Independent bootstrap project: validates that vcpkg manifest mode resolves and
# links libpng, zlib and Catch2 at the exact pinned versions recorded in
# cmake/dependencies.lock.json. It deliberately needs no Qt and creates no
# product code; WP-001 establishes the real app/core walking skeleton.

cmake_minimum_required(VERSION 3.28)
project(pnga-bootstrap-deps VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/Dependencies.cmake")

add_executable(pnga_deps_smoke version_smoke.cpp)

# Link every approved library so a real, verifiable link actually happens.
target_link_libraries(pnga_deps_smoke PRIVATE PNG::PNG ZLIB::ZLIB Catch2::Catch2)

# Inject the pinned expected versions as compile definitions; the smoke program
# compares these against the macros the linked libraries report at runtime.
pnga_deliver_dependency_versions(pnga_deps_smoke)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME deps_smoke COMMAND pnga_deps_smoke)
endif()