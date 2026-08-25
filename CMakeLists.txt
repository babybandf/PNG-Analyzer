cmake_minimum_required(VERSION 3.28)

# WP-001: root build for the PNG Analyzer walking skeleton. Qt is optional at
# configure time (GUI is added only when a Qt 6.8+ kit is found); core, CLI and
# the unit tests build and verify without it. See CMakeUserPresets.example.json
# for pointing the dev/asan/release presets at a Qt kit via CMAKE_PREFIX_PATH.
#
# The version is the single source for the binary, About dialog, CLI output and
# package names. Release builds pass -DPNGA_PROJECT_VERSION=<tag-version> so
# the released program always matches the release tag; dev builds default here.
set(PNGA_PROJECT_VERSION "0.1.0" CACHE STRING
    "PNG Analyzer version (x.y.z); release builds set this to the tag version")
project(png-analyzer VERSION ${PNGA_PROJECT_VERSION} LANGUAGES CXX)

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
option(PNGA_BUILD_COVERAGE_FUZZ
       "Build the optional LLVM libFuzzer harness (requires a fuzzer runtime)"
       OFF)
option(PNGA_ENABLE_QT_DEPLOYMENT
       "Deploy Qt frameworks/plugins during install (requires macdeployqt or windeployqt)"
       OFF)

if(PNGA_ENABLE_QT_DEPLOYMENT)
  if(NOT PNGA_HAVE_QT)
    message(FATAL_ERROR "PNGA_ENABLE_QT_DEPLOYMENT requires a Qt 6 GUI target")
  endif()
  if(APPLE)
    find_program(PNGA_QT_DEPLOY_TOOL macdeployqt)
  elseif(WIN32)
    find_program(PNGA_QT_DEPLOY_TOOL windeployqt)
  endif()
  if(NOT PNGA_QT_DEPLOY_TOOL)
    message(FATAL_ERROR
      "PNGA_ENABLE_QT_DEPLOYMENT requires the platform Qt deployment tool")
  endif()
  find_program(PNGA_QT_PATHS_TOOL NAMES qtpaths qtpaths6)
  if(NOT PNGA_QT_PATHS_TOOL)
    message(FATAL_ERROR
      "PNGA_ENABLE_QT_DEPLOYMENT requires the Qt paths tool")
  endif()
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt-deploy.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pnga-qt-deploy.cmake"
    @ONLY
  )
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/io)
add_subdirectory(libs/png-format)
add_subdirectory(libs/validation)
add_subdirectory(libs/trace-model)
add_subdirectory(libs/statistics)
add_subdirectory(libs/backend-libpng)
add_subdirectory(libs/analysis-engine)
add_subdirectory(libs/png-reconstruction)
add_subdirectory(libs/deflate-runtime)
add_subdirectory(libs/deflate-index)
add_subdirectory(libs/deflate-trace)
add_subdirectory(apps/pnga-cli)
if(PNGA_HAVE_QT)
  add_subdirectory(ui/qt)
  add_subdirectory(apps/png-analyzer-gui)
endif()

if(PNGA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# WP-605A: install the relocatable CLI (and optional GUI) targets, then let
# CPack produce the platform archive used by the package smoke gate.
include(GNUInstallDirs)
install(TARGETS pnga_cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)
# vcpkg uses shared runtime libraries on the Windows triplet.  Include the
# dependency DLLs in the archive so the extracted CLI can run without the
# build tree or a developer toolchain on PATH.  The installed tree contains
# only the manifest dependencies, so this remains scoped to the product.
if(WIN32 AND VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  file(GLOB PNGA_VCPKG_RUNTIME_DLLS CONFIGURE_DEPENDS
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll"
  )
  if(PNGA_VCPKG_RUNTIME_DLLS)
    install(FILES ${PNGA_VCPKG_RUNTIME_DLLS}
      DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endif()
if(PNGA_HAVE_QT)
  install(TARGETS pnga_analyzer_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
  )
  if(PNGA_ENABLE_QT_DEPLOYMENT)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/pnga-qt-deploy.cmake")
  endif()
  if(UNIX AND NOT APPLE)
    foreach(PNGA_ICON_SIZE 16 24 32 48 64 128 256 512)
      install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/linux/hicolor/${PNGA_ICON_SIZE}x${PNGA_ICON_SIZE}/apps/png-analyzer.png"
        DESTINATION
          "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${PNGA_ICON_SIZE}x${PNGA_ICON_SIZE}/apps"
      )
    endforeach()
    install(FILES packaging/linux/png-analyzer.desktop
      DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
  endif()
endif()
install(FILES README.md LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/png-analyzer
)
include(${CMAKE_CURRENT_SOURCE_DIR}/packaging/CPackOptions.cmake)
include(CPack)
