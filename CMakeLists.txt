cmake_minimum_required(VERSION 3.28)

# WP-001: root build for the PNG Analyzer walking skeleton. Qt is optional at
# configure time (GUI is added only when a Qt 6.8+ kit is found); core, CLI and
# the unit tests build and verify without it. See CMakeUserPresets.example.json
# for pointing the dev/asan/release presets at a Qt kit via CMAKE_PREFIX_PATH.
project(png-analyzer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Centralized discovery of the vcpkg-managed dependencies (libpng/zlib/Catch2).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# Qt is declared optional here. ADR-0003 keeps everything under libs/ Qt-free
# regardless of whether Qt is present on the machine.
set(PNGA_HAVE_QT OFF)
find_package(Qt6 6.8 QUIET COMPONENTS Core Gui Widgets)
if(Qt6_FOUND)
  set(PNGA_HAVE_QT ON)
  message(STATUS "Qt ${Qt6_VERSION} found - GUI target enabled")
else()
  message(STATUS "Qt6 not found - GUI target omitted (install Qt 6.8+ and point CMAKE_PREFIX_PATH/Qt6_DIR at it)")
endif()

option(PNGA_BUILD_TESTS "Build PNG Analyzer tests" ON)

add_subdirectory(libs/core)
add_subdirectory(libs/io)
add_subdirectory(libs/png-format)
add_subdirectory(libs/validation)
add_subdirectory(libs/trace-model)
add_subdirectory(libs/backend-libpng)
add_subdirectory(libs/analysis-engine)
add_subdirectory(libs/png-reconstruction)
add_subdirectory(libs/deflate-runtime)
add_subdirectory(libs/deflate-index)
add_subdirectory(apps/pnga-cli)
if(PNGA_HAVE_QT)
  add_subdirectory(ui/qt)
  add_subdirectory(apps/png-analyzer-gui)
endif()

if(PNGA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()