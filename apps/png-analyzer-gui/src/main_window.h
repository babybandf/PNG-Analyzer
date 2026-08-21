#ifndef PNG_ANALYZER_GUI_MAIN_WINDOW_H
#define PNG_ANALYZER_GUI_MAIN_WINDOW_H

// WP-104/204 GUI shell: wires the chunk tree and hex view to the delivered
// image view. App-level composition only; PNG parsing and decoding happen off
// the UI thread (AGENTS.md) and stale decode results never overwrite a newer
// document (generation counter).

#include <pnga/analysis-engine/reference_decode.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>
#include <pnga/trace-model/selection.h>

#include <QMainWindow>
#include <QString>
#include <QThread>

#include <cstdint>
#include <memory>

class QLabel;
class QModelIndex;
class QTreeView;

namespace pnga::ui::qt {
class ChunkModel;
class DeliveredImageView;
class HexView;
class SelectionBus;
class StageInspector;
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
  void onPixelSelected(int x, int y);

 private:
  void resetDocument();
  void startDecode();
  void startStageAnalysis();

  std::shared_ptr<pnga::io::IByteSource> source_;
  pnga::png_format::ChunkIndex index_;
  pnga::ui::qt::ChunkModel* model_ = nullptr;
  pnga::ui::qt::HexView* hex_ = nullptr;
  pnga::ui::qt::DeliveredImageView* image_view_ = nullptr;
  pnga::ui::qt::SelectionBus* bus_ = nullptr;
  pnga::ui::qt::StageInspector* inspector_ = nullptr;
  QTreeView* tree_ = nullptr;
  DecodeWorker* decode_worker_ = nullptr;
  StageWorker* stage_worker_ = nullptr;
  std::uint64_t generation_ = 0;
  QLabel* pixel_label_ = nullptr;
};

#endif  // PNG_ANALYZER_GUI_MAIN_WINDOW_H
