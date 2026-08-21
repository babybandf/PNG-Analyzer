#ifndef PNG_ANALYZER_GUI_MAIN_WINDOW_H
#define PNG_ANALYZER_GUI_MAIN_WINDOW_H

// WP-104 GUI shell: QMainWindow + QDockWidget composition that wires the chunk
// tree model to the windowed hex view. App-level composition only; it never
// parses PNG data itself (AGENTS.md).

#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_index.h>

#include <QMainWindow>
#include <QString>

#include <memory>

class QModelIndex;
class QTreeView;

namespace pnga::ui::qt {
class ChunkModel;
class HexView;
}  // namespace pnga::ui::qt

class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Opens and indexes `path`; returns false when the file cannot be read.
  bool openFile(const QString& path);

 private slots:
  void onOpenTriggered();
  void onChunkSelectionChanged(const QModelIndex& current,
                               const QModelIndex& previous);

 private:
  void resetDocument();

  std::unique_ptr<pnga::io::IByteSource> source_;
  pnga::png_format::ChunkIndex index_;
  pnga::ui::qt::ChunkModel* model_ = nullptr;
  pnga::ui::qt::HexView* hex_ = nullptr;
  QTreeView* tree_ = nullptr;
};

#endif  // PNG_ANALYZER_GUI_MAIN_WINDOW_H
