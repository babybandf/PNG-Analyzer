#ifndef PNG_ANALYZER_GUI_MAIN_WINDOW_H
#define PNG_ANALYZER_GUI_MAIN_WINDOW_H

// WP-104/204 GUI shell: wires the chunk tree and hex view to the delivered
// image view. App-level composition only; PNG parsing and decoding happen off
// the UI thread (AGENTS.md) and stale decode results never overwrite a newer
// document (generation counter).

#include <pnga/analysis-engine/query_coordinator.h>
#include <pnga/analysis-engine/reference_decode.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/analysis-engine/validation.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_detail.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/trace-model/selection.h>
#include <pnga/ui/qt/selection_view_state.h>

#include <QMainWindow>
#include <QObject>
#include <QString>
#include <QThread>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

class QLabel;
class QAction;
class QCheckBox;
class QCloseEvent;
class QDockWidget;
class QDragEnterEvent;
class QDragMoveEvent;
class QEvent;
class QMenu;
class QModelIndex;
class QPaintEvent;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTabWidget;
class QTreeView;
class QWidget;
class QDropEvent;

namespace pnga::analysis_engine {
class TraceOrchestrator;
class TraceInspectorStateMachine;
struct TraceTaskHandle;
struct TraceQueryResult;
}  // namespace pnga::analysis_engine

namespace pnga::ui::qt {
class ChunkModel;
class ChunkDetailPanel;
class BlockInspector;
class CompressionContext;
class DeliveredImageView;
class DecodeTraceInspector;
class HuffmanInspector;
class HexView;
class HexSourceTabBar;
class SelectionBus;
class StageInspector;
class StagePixelProcessView;
class TraceInspectorBinding;
class ApplicationTheme;
}  // namespace pnga::ui::qt

// WP-5U15: worker/bridge types live in document_workers.h (moved verbatim).
#include "document_session.h"
#include "document_workers.h"
#include "main_window_ui.h"
#include "selection_navigation_controller.h"
#include "trace_controller.h"
#include "workspace_controller.h"

class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr,
                      pnga::ui::qt::ApplicationTheme* theme = nullptr);
  ~MainWindow() override;

  // Opens and indexes `path`; starts background reference decode and stage
  // analysis. Returns false when the file cannot be read.
  bool openFile(const QString& path);

 private slots:
  void onOpenTriggered();
  void onCloseTriggered();
  void onDecodeDone(std::uint64_t generation);
  void onStageDone(std::uint64_t generation);
  void onValidationDone(std::uint64_t generation);
  void onChunkDetailDone(std::uint64_t generation,
                         std::uint64_t selection_serial);
  void onRowQueryStatus(std::uint64_t row, int status);
  void resetLayout();

 private:
  void openRecentFile(const QString& path);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  MainWindowWidgets widgets_;
  std::unique_ptr<WorkspaceController> workspace_;
  std::unique_ptr<DocumentSession> session_;
  std::unique_ptr<SelectionNavigationController> selection_;
  std::unique_ptr<TraceController> trace_controller_;
  pnga::ui::qt::HexView* hex_ = nullptr;
  pnga::ui::qt::DeliveredImageView* image_view_ = nullptr;
  pnga::ui::qt::StagePixelProcessView* pixel_view_ = nullptr;
  pnga::ui::qt::StagePixelProcessView* filtered_view_ = nullptr;
  pnga::ui::qt::StagePixelProcessView* defiltered_view_ = nullptr;
  pnga::ui::qt::SelectionBus* bus_ = nullptr;
  pnga::ui::qt::StageInspector* inspector_ = nullptr;
  pnga::ui::qt::BlockInspector* block_inspector_ = nullptr;
  pnga::ui::qt::HuffmanInspector* huffman_inspector_ = nullptr;
  pnga::ui::qt::DecodeTraceInspector* decode_trace_inspector_ = nullptr;
  pnga::ui::qt::TraceInspectorBinding* trace_binding_ = nullptr;
  pnga::ui::qt::CompressionContext* compression_context_ = nullptr;
  QDockWidget* chunks_dock_ = nullptr;
  QDockWidget* inspector_dock_ = nullptr;
  QSplitter* chunks_splitter_ = nullptr;
  pnga::ui::qt::ChunkDetailPanel* chunk_detail_ = nullptr;
  QTabWidget* preview_tabs_ = nullptr;
  QTabWidget* inspector_tabs_ = nullptr;
  QTabWidget* compression_inspector_tabs_ = nullptr;
  QWidget* hex_panel_ = nullptr;
  pnga::ui::qt::HexSourceTabBar* hex_source_tabs_ = nullptr;
  QSplitter* center_splitter_ = nullptr;
  QSpinBox* x_spin_ = nullptr;
  QSpinBox* y_spin_ = nullptr;
  QCheckBox* lock_check_ = nullptr;
  QPushButton* base_button_ = nullptr;
  QTreeView* tree_ = nullptr;
  QAction* close_action_ = nullptr;
  QMenu* recent_files_menu_ = nullptr;
  QLabel* pixel_label_ = nullptr;
  QLabel* validation_label_ = nullptr;
  pnga::ui::qt::ApplicationTheme* theme_ = nullptr;
};

#endif  // PNG_ANALYZER_GUI_MAIN_WINDOW_H
