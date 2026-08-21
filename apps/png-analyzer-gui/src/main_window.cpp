// WP-104 MainWindow: docks, menu, file open and chunk->hex selection wiring.

#include "main_window.h"

#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/hex_view.h>

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollBar>
#include <QTreeView>
#include <QWidget>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

namespace {

constexpr std::uint64_t kHeaderSpanLength = 8;
constexpr std::uint64_t kCrcSpanLength = 4;

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("PNG Analyzer"));

  hex_ = new pnga::ui::qt::HexView(this);
  setCentralWidget(hex_);

  auto* chunks = new QDockWidget(QStringLiteral("Chunks"), this);
  chunks->setObjectName(QStringLiteral("chunksDock"));
  tree_ = new QTreeView(chunks);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setUniformRowHeights(true);
  tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  chunks->setWidget(tree_);
  addDockWidget(Qt::LeftDockWidgetArea, chunks);

  QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  QAction* openAction = fileMenu->addAction(QStringLiteral("&Open..."));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpenTriggered);

  QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
  helpMenu->addAction(QStringLiteral("About"), this, [this] {
    QMessageBox::about(this, QStringLiteral("About PNG Analyzer"),
                       QStringLiteral("PNG Analyzer walking skeleton (WP-104)."));
  });
}

bool MainWindow::openFile(const QString& path) {
  std::unique_ptr<pnga::io::IByteSource> source;
  const std::error_code ec =
      pnga::io::open_mapped_file(std::filesystem::path(path.toStdString()),
                                 source);
  if (ec) {
    return false;
  }
  source_ = std::move(source);
  index_ = pnga::png_format::index_chunks(*source_);
  resetDocument();
  return true;
}

void MainWindow::onOpenTriggered() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Open PNG"), QString(),
      QStringLiteral("PNG files (*.png);;All files (*)"));
  if (path.isEmpty()) {
    return;
  }
  if (!openFile(path)) {
    QMessageBox::warning(this, QStringLiteral("PNG Analyzer"),
                         QStringLiteral("Could not open file:\n%1").arg(path));
  }
}

void MainWindow::resetDocument() {
  if (model_ != nullptr) {
    delete model_;
  }
  model_ = new pnga::ui::qt::ChunkModel(&index_, this);
  tree_->setModel(model_);
  hex_->setSource(source_.get());

  if (model_->rowCount() > 0) {
    tree_->selectionModel()->setCurrentIndex(
        model_->index(0, 0),
        QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
  }
}

void MainWindow::onChunkSelectionChanged(const QModelIndex& current,
                                         const QModelIndex& /*previous*/) {
  if (!current.isValid()) {
    hex_->clearHighlight();
    return;
  }
  const auto& node = model_->chunkAt(current.row());

  std::vector<pnga::ui::qt::HexHighlightSpan> spans;
  spans.push_back({node.header_offset, kHeaderSpanLength,
                   QColor(0x9E, 0x9E, 0x9E)});  // header: gray
  spans.push_back({node.data_offset, node.data_length,
                   QColor(0x42, 0xA5, 0xF5)});  // data: blue
  spans.push_back({node.crc_offset, kCrcSpanLength,
                   QColor(0x66, 0xBB, 0x6A)});  // CRC: green
  hex_->setHighlight(std::move(spans));

  hex_->verticalScrollBar()->setValue(static_cast<int>(
      node.header_offset / 16));
}
