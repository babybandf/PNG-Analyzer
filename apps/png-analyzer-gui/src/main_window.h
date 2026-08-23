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
#include <pnga/png-format/chunk_index.h>
#include <pnga/trace-model/selection.h>
#include <pnga/ui/qt/selection_view_state.h>

#include <QMainWindow>
#include <QObject>
#include <QString>
#include <QThread>

#include <cstdint>
#include <memory>

class QLabel;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QEvent;
class QModelIndex;
class QSpinBox;
class QSplitter;
class QTabWidget;
class QTreeView;

namespace pnga::ui::qt {
class ChunkModel;
class BlockInspector;
class DeliveredImageView;
class DecodeTraceInspector;
class HuffmanInspector;
class HexView;
class PixelViewport;
class SelectionBus;
class StageInspector;
class StagePreviewView;
}  // namespace pnga::ui::qt

namespace {
constexpr int kChunkPanelOrigin = 1;
constexpr int kImagePanelOrigin = 2;
constexpr int kHexPanelOrigin = 3;
}  // namespace

// Decodes a shared source on a worker thread. Owns its own source copy so a
// newly opened file cannot invalidate an in-flight decode.
class DecodeWorker final : public QThread {
  Q_OBJECT
 public:
  DecodeWorker(std::uint64_t generation,
               std::shared_ptr<pnga::io::IByteSource> source,
               QObject* parent = nullptr);

  std::uint64_t generation() const noexcept { return generation_; }
  pnga::backend_libpng::ReferenceResult result() const { return result_; }

 signals:
  void decodeDone(std::uint64_t generation);

 protected:
  void run() override;

 private:
  std::uint64_t generation_;
  std::shared_ptr<pnga::io::IByteSource> source_;
  pnga::backend_libpng::ReferenceResult result_;
};

// Materializes the Filtered/Unfiltered/Native stage set on a worker thread.
// Shares the source ownership so a newly opened file cannot invalidate it.
class StageWorker final : public QThread {
  Q_OBJECT
 public:
  StageWorker(std::uint64_t generation,
              std::shared_ptr<pnga::io::IByteSource> source,
              QObject* parent = nullptr);

  std::uint64_t generation() const noexcept { return generation_; }
  std::shared_ptr<const pnga::analysis_engine::StageSet> result() const {
    return result_;
  }

 signals:
  void stageDone(std::uint64_t generation);

 protected:
  void run() override;

 private:
  std::uint64_t generation_;
  std::shared_ptr<pnga::io::IByteSource> source_;
  std::shared_ptr<pnga::analysis_engine::StageSet> result_;
};

// Runs the complete Qt-free validation bundle off the GUI thread. The copied
// ChunkIndex preserves deterministic structure while shared source ownership
// keeps every borrowed range alive until publication.
class ValidationWorker final : public QThread {
  Q_OBJECT
 public:
  ValidationWorker(std::uint64_t generation,
                   std::shared_ptr<pnga::io::IByteSource> source,
                   pnga::png_format::ChunkIndex index,
                   QObject* parent = nullptr);

  std::uint64_t generation() const noexcept { return generation_; }
  pnga::analysis_engine::DocumentValidationReport result() const {
    return result_;
  }

 signals:
  void validationDone(std::uint64_t generation);

 protected:
  void run() override;

 private:
  std::uint64_t generation_;
  std::shared_ptr<pnga::io::IByteSource> source_;
  pnga::png_format::ChunkIndex index_;
  pnga::analysis_engine::DocumentValidationReport result_;
};

// Bridges the Qt-free QueryCoordinator's worker-thread status callback onto the
// GUI thread via a queued signal.
class QueryStatusBridge final : public QObject {
  Q_OBJECT
 public:
  explicit QueryStatusBridge(QObject* parent = nullptr) : QObject(parent) {}
 signals:
  void rowStatus(std::uint64_t row, int status);
};

class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Opens and indexes `path`; starts background reference decode and stage
  // analysis. Returns false when the file cannot be read.
  bool openFile(const QString& path);

 private slots:
  void onOpenTriggered();
  void onChunkSelectionChanged(const QModelIndex& current,
                               const QModelIndex& previous);
  void onDecodeDone(std::uint64_t generation);
  void onStageDone(std::uint64_t generation);
  void onValidationDone(std::uint64_t generation);
  void onPixelSelected(int x, int y);
  void onRowQueryStatus(std::uint64_t row, int status);
  void resetLayout();

 private:
  void resetDocument();
  void startDecode();
  void startStageAnalysis();
  void startValidation();
  void openQueryCoordinator(const pnga::png_reconstruction::ImageHeader& header);
  void restoreWorkspace();
  void saveWorkspace() const;
  void applyDefaultWorkspace();
  void configureDockInteraction();
  void publishLockedCoordinate();
  void clearLockedCoordinate();
  void nudgeLockedCoordinate(int dx, int dy);
  void updateHexSource();

 protected:
  void closeEvent(QCloseEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  std::shared_ptr<pnga::io::IByteSource> source_;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stage_set_;
  pnga::png_format::ChunkIndex index_;
  pnga::ui::qt::ChunkModel* model_ = nullptr;
  pnga::ui::qt::HexView* hex_ = nullptr;
  pnga::ui::qt::DeliveredImageView* image_view_ = nullptr;
  pnga::ui::qt::PixelViewport* pixel_view_ = nullptr;
  pnga::ui::qt::StagePreviewView* filter_map_view_ = nullptr;
  pnga::ui::qt::StagePreviewView* filtered_view_ = nullptr;
  pnga::ui::qt::StagePreviewView* defiltered_view_ = nullptr;
  pnga::ui::qt::SelectionBus* bus_ = nullptr;
  pnga::ui::qt::StageInspector* inspector_ = nullptr;
  pnga::ui::qt::BlockInspector* block_inspector_ = nullptr;
  pnga::ui::qt::HuffmanInspector* huffman_inspector_ = nullptr;
  pnga::ui::qt::DecodeTraceInspector* decode_trace_inspector_ = nullptr;
  pnga::ui::qt::SelectionViewState view_state_;
  QDockWidget* chunks_dock_ = nullptr;
  QDockWidget* inspector_dock_ = nullptr;
  QTabWidget* preview_tabs_ = nullptr;
  QTabWidget* inspector_tabs_ = nullptr;
  QTabWidget* image_inspector_tabs_ = nullptr;
  QTabWidget* scanline_inspector_tabs_ = nullptr;
  QTabWidget* compression_inspector_tabs_ = nullptr;
  QSplitter* center_splitter_ = nullptr;
  QSpinBox* x_spin_ = nullptr;
  QSpinBox* y_spin_ = nullptr;
  QCheckBox* lock_check_ = nullptr;
  QCheckBox* hex_follow_check_ = nullptr;
  QComboBox* base_combo_ = nullptr;
  QComboBox* hex_source_combo_ = nullptr;
  QTreeView* tree_ = nullptr;
  DecodeWorker* decode_worker_ = nullptr;
  StageWorker* stage_worker_ = nullptr;
  ValidationWorker* validation_worker_ = nullptr;
  std::unique_ptr<pnga::analysis_engine::QueryCoordinator> query_;
  QueryStatusBridge* query_bridge_ = nullptr;
  std::uint64_t generation_ = 0;
  QLabel* pixel_label_ = nullptr;
  QLabel* validation_label_ = nullptr;
  pnga::analysis_engine::DocumentValidationReport validation_report_;
};

#endif  // PNG_ANALYZER_GUI_MAIN_WINDOW_H
