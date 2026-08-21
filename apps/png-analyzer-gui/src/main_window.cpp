// WP-104/204 MainWindow: docks, menu, file open, chunk->hex selection and the
// background reference decode into the delivered image view.

#include "main_window.h"

#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_view.h>

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QDockWidget>
#include <QFileDialog>
#include <QHeaderView>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollBar>
#include <QStatusBar>
#include <QTreeView>
#include <QWidget>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

namespace {

constexpr std::uint64_t kHeaderSpanLength = 8;
constexpr std::uint64_t kCrcSpanLength = 4;

}  // namespace

// ---------------------------------------------------------------------------
// DecodeWorker
// ---------------------------------------------------------------------------

DecodeWorker::DecodeWorker(std::uint64_t generation,
                           std::shared_ptr<pnga::io::IByteSource> source,
                           QObject* parent)
    : QThread(parent),
      generation_(generation),
      source_(std::move(source)) {}

void DecodeWorker::run() {
  result_ = pnga::analysis_engine::decode_reference(*source_);
  emit decodeDone(generation_);
}

// ---------------------------------------------------------------------------
// MainWindow
// ---------------------------------------------------------------------------

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

  image_view_ = new pnga::ui::qt::DeliveredImageView(this);
  auto* imageDock = new QDockWidget(QStringLiteral("Delivered Image"), this);
  imageDock->setObjectName(QStringLiteral("imageDock"));
  imageDock->setWidget(image_view_);
  addDockWidget(Qt::RightDockWidgetArea, imageDock);

  pixel_label_ = new QLabel(QStringLiteral("No image"), this);
  statusBar()->addWidget(pixel_label_);

  QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  QAction* openAction = fileMenu->addAction(QStringLiteral("&Open..."));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpenTriggered);

  QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
  helpMenu->addAction(QStringLiteral("About"), this, [this] {
    QMessageBox::about(this, QStringLiteral("About PNG Analyzer"),
                       QStringLiteral("PNG Analyzer (WP-104/204)."));
  });

  // Initial empty model so the selection-model connection is valid from the
  // start; resetDocument() reconnects after each real setModel.
  model_ = new pnga::ui::qt::ChunkModel(&index_, this);
  tree_->setModel(model_);
  connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &MainWindow::onChunkSelectionChanged);

  connect(image_view_, &pnga::ui::qt::DeliveredImageView::pixelSelected,
          this, &MainWindow::onPixelSelected);
}

bool MainWindow::openFile(const QString& path) {
  std::unique_ptr<pnga::io::IByteSource> opened;
  const std::error_code ec = pnga::io::open_mapped_file(
      std::filesystem::path(path.toStdString()), opened);
  if (ec) {
    return false;
  }
  // Shared ownership so an in-flight worker keeps its source alive even when a
  // newer file replaces source_ (virtual dtor makes this safe).
  auto source =
      std::shared_ptr<pnga::io::IByteSource>(opened.release());
  source_ = source;
  index_ = pnga::png_format::index_chunks(*source_);
  resetDocument();
  startDecode();
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
  // setModel() replaces the selection model; reconnect to the new one.
  connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &MainWindow::onChunkSelectionChanged);
  hex_->setSource(source_.get());

  if (model_->rowCount() > 0) {
    tree_->selectionModel()->setCurrentIndex(
        model_->index(0, 0),
        QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
  }
}

void MainWindow::startDecode() {
  ++generation_;
  if (decode_worker_ != nullptr) {
    // A previous decode may still be running; its result will be ignored
    // because its generation is stale. Drop the reference now.
    decode_worker_ = nullptr;
  }
  auto* worker = new DecodeWorker(generation_, source_, this);
  decode_worker_ = worker;
  connect(worker, &DecodeWorker::decodeDone, this, &MainWindow::onDecodeDone);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void MainWindow::onDecodeDone(std::uint64_t generation) {
  if (generation != generation_ || decode_worker_ == nullptr) {
    return;  // stale decode; never overwrite the current document's image
  }
  const auto& result = decode_worker_->result();
  if (!result.success) {
    image_view_->setImage(QImage());
    pixel_label_->setText(QStringLiteral("decode failed: %1")
                              .arg(QString::fromStdString(result.error)));
    decode_worker_ = nullptr;
    return;
  }
  const auto& img = result.image;
  QImage qimage(static_cast<int>(img.width), static_cast<int>(img.height),
                QImage::Format_RGBA8888);
  std::memcpy(qimage.bits(), img.rgba.data(), img.rgba.size());
  image_view_->setImage(qimage);
  pixel_label_->setText(QStringLiteral("%1 x %2  (bit depth %3, color type %4)")
                            .arg(img.width)
                            .arg(img.height)
                            .arg(img.source_bit_depth)
                            .arg(img.source_color_type));
  decode_worker_ = nullptr;
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

  hex_->verticalScrollBar()->setValue(
      static_cast<int>(node.header_offset / 16));
}

void MainWindow::onPixelSelected(int x, int y) {
  pnga::trace_model::Selection sel;
  sel.image = pnga::trace_model::ImageCoordinate{0, 0, 0,
                                                 static_cast<std::uint64_t>(x),
                                                 static_cast<std::uint64_t>(y),
                                                 0};
  sel.stage = pnga::trace_model::Stage::kDelivered;
  const auto rgba = image_view_->rgbaAt(x, y);
  if (rgba.has_value()) {
    pixel_label_->setText(
        QStringLiteral("pixel (%1, %2) RGBA(%3, %4, %5, %6)")
            .arg(x)
            .arg(y)
            .arg((*rgba)[0])
            .arg((*rgba)[1])
            .arg((*rgba)[2])
            .arg((*rgba)[3]));
  }
}
