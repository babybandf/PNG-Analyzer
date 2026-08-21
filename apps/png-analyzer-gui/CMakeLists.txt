# apps/png-analyzer-gui — desktop GUI entry point (REPOSITORY_LAYOUT.md §4).
# The only Qt-bearing executable added by this subdir; it is only ever added to
# the build when Qt6 was found at the root. GUI code must not parse or decode
# PNG data (AGENTS.md); it composes pnga::ui_qt models and widgets. The chunk
# index is built by libs/png-format; the app wires it to the UI.

add_executable(pnga_analyzer_gui
  src/main.cpp
  src/main_window.cpp
)
set_target_properties(pnga_analyzer_gui PROPERTIES AUTOMOC ON)
target_link_libraries(pnga_analyzer_gui PRIVATE
  pnga::ui_qt
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  pnga::core
)
target_compile_features(pnga_analyzer_gui PRIVATE cxx_std_20)