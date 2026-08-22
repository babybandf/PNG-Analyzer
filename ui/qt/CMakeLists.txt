# ui/qt — Qt UI library (REPOSITORY_LAYOUT.md §4, §8.1; ADR-0003).
# The only Qt-bearing library in the repository; everything under libs/ stays
# Qt-free. WP-104 provides the Chunk tree model and the windowed Hex view.
#
# Interim dependency note: pnga_ui_qt composes png_format/io directly until the
# M2 analysis engine (libs/analysis-engine) exists; it will then depend on
# pnga_analysis_engine + pnga_trace_model + pnga_rendering per layout §7.

add_library(pnga_ui_qt
  include/pnga/ui/qt/about_dialog.h
  include/pnga/ui/qt/chunk_model.h
  include/pnga/ui/qt/delivered_image_view.h
  include/pnga/ui/qt/hex_view.h
  include/pnga/ui/qt/image_transform.h
  include/pnga/ui/qt/pixel_viewport.h
  include/pnga/ui/qt/selection_bus.h
  include/pnga/ui/qt/selection_view_state.h
  include/pnga/ui/qt/stage_inspector.h
  include/pnga/ui/qt/stage_inspector_model.h
  src/about_dialog.cpp
  src/chunk_model.cpp
  src/delivered_image_view.cpp
  src/hex_view.cpp
  src/image_transform.cpp
  src/pixel_viewport.cpp
  src/selection_bus.cpp
  src/stage_inspector.cpp
  src/stage_inspector_model.cpp
)
add_library(pnga::ui_qt ALIAS pnga_ui_qt)

target_include_directories(pnga_ui_qt PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(pnga_ui_qt PUBLIC
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  pnga::analysis_engine
  pnga::png_format
  pnga::io
  pnga::core
  pnga::trace_model
)
set_target_properties(pnga_ui_qt PROPERTIES
  AUTOMOC ON
  POSITION_INDEPENDENT_CODE ON
)
target_compile_features(pnga_ui_qt PUBLIC cxx_std_20)
