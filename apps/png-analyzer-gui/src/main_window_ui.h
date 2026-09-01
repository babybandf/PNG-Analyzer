#ifndef PNG_ANALYZER_GUI_MAIN_WINDOW_UI_H
#define PNG_ANALYZER_GUI_MAIN_WINDOW_UI_H

// WP-5U15: deterministic MainWindow construction. buildMainWindowUi creates
// every widget, dock, menu and action with the exact parents, object names,
// accessible names, tab order and initial state of the original facade
// constructor. It owns no document, workspace, selection or trace behavior
// and does not connect anything to MainWindow slots.
//
// pnga widget types are only forward-declared here: every field is a pointer
// and pulling their full headers into this facade header would drag
// analysis-engine trace types into every moc TU. The builder .cpp includes
// them fully.

#include <QAction>
#include <QCheckBox>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeView>
#include <QWidget>

namespace pnga::ui::qt {
class ApplicationTheme;
class BlockInspector;
class ChunkDetailPanel;
class CompressionContext;
class DeliveredImageView;
class DecodeTraceInspector;
class HexSourceTabBar;
class HexView;
class HuffmanInspector;
class SelectionBus;
class StageInspector;
class StagePixelProcessView;
class TraceInspectorBinding;
}  // namespace pnga::ui::qt

struct MainWindowWidgets final {
  pnga::ui::qt::HexView* hex = nullptr;
  pnga::ui::qt::DeliveredImageView* image_view = nullptr;
  pnga::ui::qt::StagePixelProcessView* pixel_view = nullptr;
  pnga::ui::qt::StagePixelProcessView* filtered_view = nullptr;
  pnga::ui::qt::StagePixelProcessView* defiltered_view = nullptr;
  pnga::ui::qt::SelectionBus* bus = nullptr;
  pnga::ui::qt::StageInspector* inspector = nullptr;
  pnga::ui::qt::BlockInspector* block_inspector = nullptr;
  pnga::ui::qt::HuffmanInspector* huffman_inspector = nullptr;
  pnga::ui::qt::DecodeTraceInspector* decode_trace_inspector = nullptr;
  pnga::ui::qt::TraceInspectorBinding* trace_binding = nullptr;
  pnga::ui::qt::CompressionContext* compression_context = nullptr;
  QDockWidget* chunks_dock = nullptr;
  QDockWidget* inspector_dock = nullptr;
  QSplitter* chunks_splitter = nullptr;
  pnga::ui::qt::ChunkDetailPanel* chunk_detail = nullptr;
  QTabWidget* preview_tabs = nullptr;
  QTabWidget* inspector_tabs = nullptr;
  QTabWidget* compression_inspector_tabs = nullptr;
  QWidget* hex_panel = nullptr;
  pnga::ui::qt::HexSourceTabBar* hex_source_tabs = nullptr;
  QSplitter* center_splitter = nullptr;
  QSpinBox* x_spin = nullptr;
  QSpinBox* y_spin = nullptr;
  QCheckBox* lock_check = nullptr;
  QPushButton* base_button = nullptr;
  QTreeView* tree = nullptr;
  QLabel* pixel_label = nullptr;
  QLabel* validation_label = nullptr;
  QAction* open_action = nullptr;
  QAction* close_action = nullptr;
  QAction* exit_action = nullptr;
  QAction* reset_layout_action = nullptr;
  QAction* show_hex_view_action = nullptr;
  QAction* show_chunks_action = nullptr;
  QAction* show_inspector_action = nullptr;
  QMenu* recent_files_menu = nullptr;
  QAction* theme_system_action = nullptr;
  QAction* theme_light_action = nullptr;
  QAction* theme_dark_action = nullptr;
};

// Creates the complete widget/dock/menu/action graph of the analyzer window.
// `theme` may be null (standalone tests); the theme menu is then omitted.
MainWindowWidgets buildMainWindowUi(
    QMainWindow& window, pnga::ui::qt::ApplicationTheme* theme);

#endif  // PNG_ANALYZER_GUI_MAIN_WINDOW_UI_H
