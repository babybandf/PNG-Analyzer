#ifndef PNG_ANALYZER_GUI_MAIN_WINDOW_H
#define PNG_ANALYZER_GUI_MAIN_WINDOW_H

// WP-104/204 GUI shell: wires the chunk tree and hex view to the delivered
// image view. App-level composition only; PNG parsing and decoding happen off
// the UI thread (AGENTS.md) and stale decode results never overwrite a newer
// document (generation counter).

#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMainWindow>
#include <QPaintEvent>
#include <QString>

#include <cstdint>
#include <memory>

#include <pnga/ui/qt/compression_selection_store.h>

namespace pnga::ui::qt {
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
  // WP-5U12B: exactly one shared Compression selection store.
  pnga::ui::qt::CompressionSelectionStore compression_store_;
  std::unique_ptr<WorkspaceController> workspace_;
  std::unique_ptr<DocumentSession> session_;
  std::unique_ptr<SelectionNavigationController> selection_;
  std::unique_ptr<TraceController> trace_;
};

#endif  // PNG_ANALYZER_GUI_MAIN_WINDOW_H
