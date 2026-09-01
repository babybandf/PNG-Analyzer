# apps/png-analyzer-gui — desktop GUI entry point (REPOSITORY_LAYOUT.md §4).
# The only Qt-bearing executable added by this subdir; it is only ever added to
# the build when Qt6 was found at the root. GUI code must not parse or decode
# PNG data (AGENTS.md); it composes pnga::ui_qt models and widgets. The chunk
# index is built by libs/png-format; the app wires it to the UI.

set(PNGA_GUI_PLATFORM_SOURCES)
if(WIN32)
  enable_language(RC)
  set(PNGA_WINDOWS_ICON_PATH
      "${CMAKE_SOURCE_DIR}/packaging/icons/png-analyzer.ico")
  configure_file(
    src/png-analyzer.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/png-analyzer.rc"
    @ONLY
  )
  list(APPEND PNGA_GUI_PLATFORM_SOURCES
       "${CMAKE_CURRENT_BINARY_DIR}/png-analyzer.rc")
elseif(APPLE)
  set(PNGA_MACOS_ICON
      "${CMAKE_SOURCE_DIR}/packaging/icons/png-analyzer.icns")
  set_source_files_properties("${PNGA_MACOS_ICON}" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
  )
  list(APPEND PNGA_GUI_PLATFORM_SOURCES "${PNGA_MACOS_ICON}")
endif()

# WP-5U15: one application-source list shared by the executable and the
# MainWindow test executables so extraction tasks keep source parity.
set(PNGA_GUI_APP_SOURCES
  src/main_window.cpp
  src/document_workers.cpp
  src/main_window_ui.cpp
  src/workspace_controller.cpp
)

add_executable(pnga_analyzer_gui
  src/main.cpp
  ${PNGA_GUI_APP_SOURCES}
  ${PNGA_GUI_PLATFORM_SOURCES}
)
set_target_properties(pnga_analyzer_gui PROPERTIES
  AUTOMOC ON
  # WP-605F: give macdeployqt a native application bundle to deploy. The
  # Windows target remains a normal GUI executable and is handled by
  # windeployqt in the packaging smoke runner.
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_ICON_FILE "png-analyzer.icns"
  WIN32_EXECUTABLE ON
)
target_link_libraries(pnga_analyzer_gui PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  pnga::core
)
target_compile_features(pnga_analyzer_gui PRIVATE cxx_std_20)
