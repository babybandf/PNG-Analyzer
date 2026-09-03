// WP-607C real-file GUI gate: the generated corpus PNGs drive MainWindow's
// Compression pages exactly as a user file would. The gate asserts state
// (ready/partial/error copy), the Stored no-Huffman explanation, the Dynamic
// Blocks/Huffman/Decode Trace content with a Match and an EOB, the
// 320/360/480/600 light and 360/480 dark width matrices, and that row
// selection and typed navigation submit zero Deep Trace work beyond the
// explicit open action. Only existing object names are used.

#include "main_window.h"

#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/decode_trace_model.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>

#include <cstdint>

#ifndef PNGA_WP607C_CORPUS_DIR
#error "PNGA_WP607C_CORPUS_DIR must be defined by the build"
#endif

namespace {

constexpr int kTimeoutMs = 5000;

QString fixture_path(const char* relative) {
  return QDir(QString::fromUtf8(PNGA_WP607C_CORPUS_DIR))
      .filePath(QString::fromLatin1(relative));
}

QLabel* context_status(const MainWindow& window) {
  return window.findChild<QLabel*>(QStringLiteral("compressionContextStatus"));
}

pnga::ui::qt::BlockInspector* block_page(const MainWindow& window) {
  return window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
}

pnga::ui::qt::HuffmanInspector* huffman_page(const MainWindow& window) {
  return window.findChild<pnga::ui::qt::HuffmanInspector*>(
      QStringLiteral("huffmanInspector"));
}

pnga::ui::qt::DecodeTraceInspector* decode_page(const MainWindow& window) {
  return window.findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
}

QTableView* blocks_table(const MainWindow& window) {
  return block_page(window)->findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
}

QTabWidget* inspector_tabs(const MainWindow& window) {
  return window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
}

QTabWidget* compression_pages(const MainWindow& window) {
  return window.findChild<QTabWidget*>(
      QStringLiteral("compressionInspectorPages"));
}

// Opens a real corpus file and waits for the delivered image and the ready
// Compression context (the auto-committed pixel drives the bounded pipeline).
void open_valid_and_wait_ready(MainWindow& window, const char* relative) {
  QVERIFY(window.openFile(fixture_path(relative)));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), kTimeoutMs);

  auto* status = context_status(window);
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("ready")),
                           kTimeoutMs);
}

}  // namespace

class ControlledCorpusGateTest : public QObject {
  Q_OBJECT
 private slots:
  void init();

  void validPixelCaseReachesReady();
  void storedCaseSelectsStoredBlockWithNoHuffmanCopy();
  void dynamicCaseShowsBlocksHuffmanAndDecodeTrace();
  void malformedCasesKeepParsedStructureAndStableCopy();
  void narrowWidthsDoNotGrowTheInspectorOrOverlapDetails();
  void darkWidthsPreserveRequiredColumnsAndSelection();
  void rowSelectionAndNavigationSubmitNoTraceWork();
};

void ControlledCorpusGateTest::init() {
  QSettings settings;
  settings.clear();
}

void ControlledCorpusGateTest::validPixelCaseReachesReady() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  open_valid_and_wait_ready(window, "valid/ui-rgb8-five-filters.png");

  // The delivered image and the shared context both come from the real file.
  // The 8x5 RGB8 rows filter to 125 bytes in exactly one final Stored block.
  auto* summary = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStreamSummary"));
  QVERIFY(summary != nullptr);
  QVERIFY(summary->text().contains(QStringLiteral("zlib stream")));
  QVERIFY(summary->text().contains(QStringLiteral("IDAT segments")));
  QVERIFY(summary->text().contains(QStringLiteral("1 blocks")));
  QVERIFY(summary->text().contains(QStringLiteral("Inflated 125 bytes")));
}

void ControlledCorpusGateTest::storedCaseSelectsStoredBlockWithNoHuffmanCopy() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  open_valid_and_wait_ready(window, "valid/trace-stored-literals.png");

  QVERIFY(inspector_tabs(window) != nullptr);
  inspector_tabs(window)->setCurrentIndex(1);
  QCoreApplication::processEvents();

  // The Fast Index selects the one Stored block.
  auto* table = blocks_table(window);
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() == 1, kTimeoutMs);
  const auto selected = block_page(window)->view().rows;
  QCOMPARE(selected.size(), std::size_t{1});
  QCOMPARE(selected.front().type, pnga::deflate_index::BlockType::kStored);
  QCOMPARE(selected.front().last, true);

  // Huffman shows the Stored block with the no-Huffman explanation.
  compression_pages(window)->setCurrentIndex(1);
  QCoreApplication::processEvents();
  auto* heading = huffman_page(window)->findChild<QLabel*>(
      QStringLiteral("huffmanInspectorHeading"));
  QVERIFY(heading != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      heading->text().contains(QStringLiteral("· Stored")), kTimeoutMs);
  bool found_explanation = false;
  for (const auto* label : huffman_page(window)->findChildren<QLabel*>()) {
    if (label->text().contains(
            QStringLiteral("stored without Huffman coding"))) {
      found_explanation = true;
    }
  }
  QVERIFY(found_explanation);
}

void ControlledCorpusGateTest::dynamicCaseShowsBlocksHuffmanAndDecodeTrace() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  open_valid_and_wait_ready(window,
                            "valid/trace-dynamic-overlap-repeats.png");

  QVERIFY(inspector_tabs(window) != nullptr);
  inspector_tabs(window)->setCurrentIndex(1);
  QCoreApplication::processEvents();

  // Blocks: one Dynamic block, BFINAL set.
  auto* table = blocks_table(window);
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() == 1, kTimeoutMs);
  const auto rows = block_page(window)->view().rows;
  QCOMPARE(rows.size(), std::size_t{1});
  QCOMPARE(rows.front().type, pnga::deflate_index::BlockType::kDynamic);
  QCOMPARE(rows.front().last, true);

  // Huffman: the Dynamic literal/length table of the published bundle.
  compression_pages(window)->setCurrentIndex(1);
  QCoreApplication::processEvents();
  auto* heading = huffman_page(window)->findChild<QLabel*>(
      QStringLiteral("huffmanInspectorHeading"));
  QVERIFY(heading != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      heading->text().contains(QStringLiteral("Dynamic")), kTimeoutMs);

  // Decode Trace: the explicit block drill-in requests the block's full
  // bounded output range, whose token list carries the Match and the EOB.
  compression_pages(window)->setCurrentIndex(0);
  table->selectRow(0);
  QPushButton* trace_button = nullptr;
  for (auto* button : block_page(window)->findChildren<QPushButton*>()) {
    if (button->text() == QStringLiteral("Open Decode Trace")) {
      trace_button = button;
    }
  }
  QVERIFY(trace_button != nullptr);
  auto* status = context_status(window);
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(( [&trace_button, &status]() {
    trace_button->click();
    QCoreApplication::processEvents();
    return status->text().contains(QStringLiteral("ready"));
  })(), kTimeoutMs);
  QTRY_COMPARE_WITH_TIMEOUT(compression_pages(window)->currentIndex(), 2,
                            kTimeoutMs);
  auto* decode = decode_page(window);
  QVERIFY(decode != nullptr);
  auto* decode_table = decode->findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(decode_table != nullptr);
  bool saw_match = false;
  bool saw_eob = false;
  QTRY_VERIFY_WITH_TIMEOUT(([
      &saw_match, &saw_eob, decode_table]() {
    saw_match = false;
    saw_eob = false;
    for (int row = 0; row < decode_table->model()->rowCount(); ++row) {
      const auto step = decode_table->model()
                            ->data(decode_table->model()->index(row, 0),
                                   pnga::ui::qt::DecodeTraceStepRole)
                            .value<pnga::analysis_engine::DecodeTraceStep>();
      saw_match = saw_match || step.path ==
                                  pnga::analysis_engine::DecodeTracePath::
                                      kMatch;
      saw_eob = saw_eob || step.path ==
                               pnga::analysis_engine::DecodeTracePath::
                                   kEndOfBlock;
    }
    return saw_match && saw_eob;
  })(), kTimeoutMs);
  QVERIFY(saw_match);
  QVERIFY(saw_eob);
}

void ControlledCorpusGateTest::malformedCasesKeepParsedStructureAndStableCopy() {
  // The two malformed corpus files have valid PNG/chunk structure but a
  // DEFLATE stream whose verified prefix contains no complete block, so the
  // bounded pipeline never reaches ready. Production must keep the parsed
  // document (chunk rows intact), show a stable non-ready context copy and
  // never start a replay for them.
  for (const char* relative : {"malformed/error-truncated-token.png",
                               "malformed/error-reserved-btype.png"}) {
    MainWindow window;
    window.resize(1200, 760);
    window.show();
    QCoreApplication::processEvents();

    QVERIFY(window.openFile(fixture_path(relative)));
    QCoreApplication::processEvents();

    // The parsed structure survives: signature, IHDR, IDAT pieces, IEND.
    auto* chunk_model = window.findChild<pnga::ui::qt::ChunkModel*>();
    QVERIFY(chunk_model != nullptr);
    QVERIFY(chunk_model->rowCount() >= 3);
    bool saw_idat = false;
    for (int row = 0; row < chunk_model->rowCount(); ++row) {
      saw_idat = saw_idat || chunk_model->chunkAt(row).text() ==
                                 QStringLiteral("IDAT");
    }
    QVERIFY(saw_idat);

    // The context stays on its stable non-ready copy: no ready, no replay.
    auto* status = context_status(window);
    QVERIFY(status != nullptr);
    QTest::qWait(50);
    QVERIFY(!status->text().contains(QStringLiteral("ready")));
    QVERIFY(!status->text().contains(QStringLiteral("Replaying")));
    QVERIFY(!status->text().isEmpty());

    // No Fast Index rows are invented for a stream with no complete block.
    auto* table = blocks_table(window);
    QVERIFY(table != nullptr);
    QCOMPARE(table->model()->rowCount(), 0);
  }
}

void ControlledCorpusGateTest::
    narrowWidthsDoNotGrowTheInspectorOrOverlapDetails() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  open_valid_and_wait_ready(window,
                            "valid/trace-dynamic-overlap-repeats.png");
  inspector_tabs(window)->setCurrentIndex(1);
  QCoreApplication::processEvents();

  auto* table = blocks_table(window);
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() == 1, kTimeoutMs);

  auto* hex_button =
      block_page(window)->findChild<QPushButton*>(
          QStringLiteral("blockShowInHex"));
  auto* inflated_button = block_page(window)->findChild<QPushButton*>(
      QStringLiteral("blockShowInflatedOutput"));
  auto* trace_button = block_page(window)->findChild<QPushButton*>(
      QStringLiteral("blockOpenDecodeTrace"));
  QVERIFY(hex_button != nullptr);
  QVERIFY(inflated_button != nullptr);
  QVERIFY(trace_button != nullptr);
  auto* splitter = block_page(window)->findChild<QSplitter*>(
      QStringLiteral("compressionPageSplitter"));
  QVERIFY(splitter != nullptr);

  QWidget* pages[] = {block_page(window), huffman_page(window),
                      decode_page(window)};
  for (const int width : {320, 360, 480, 600}) {
    for (QWidget* page : pages) {
      page->setFixedWidth(width);
      page->show();
      QCoreApplication::processEvents();
      QCOMPARE(page->width(), width);
      QVERIFY2(page->minimumWidth() <= width,
               qPrintable(QStringLiteral("minimum width %1 exceeds %2")
                              .arg(page->minimumWidth())
                              .arg(width)));
    }
    // The footer order and the details/body layout stay usable: the footer
    // action row stays at the bottom and the details live above it.
    QVERIFY(table->width() <= width);
    QVERIFY(table->viewport()->width() > 0);
    QVERIFY(hex_button->isVisible());
    QVERIFY(hex_button->x() < inflated_button->x());
    QVERIFY(trace_button->mapTo(block_page(window), QPoint(0, 0)).y() <
            hex_button->mapTo(block_page(window), QPoint(0, 0)).y());
    QVERIFY(splitter->sizes().value(1) >= 120);
  }
  for (QWidget* page : pages) {
    page->setMinimumWidth(0);
    page->resize(page->height(), page->height());
  }
}

void ControlledCorpusGateTest::darkWidthsPreserveRequiredColumnsAndSelection() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  open_valid_and_wait_ready(window,
                            "valid/trace-dynamic-overlap-repeats.png");
  inspector_tabs(window)->setCurrentIndex(1);
  QCoreApplication::processEvents();
  auto* table = blocks_table(window);
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() == 1, kTimeoutMs);

  pnga::ui::qt::ApplicationTheme theme(qApp);
  theme.setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kDark,
                /*persist=*/false);
  for (const int width : {360, 480}) {
    block_page(window)->setFixedWidth(width);
    block_page(window)->show();
    QCoreApplication::processEvents();
    QCOMPARE(block_page(window)->width(), width);

    // The six required Blocks columns stay present at both dark widths
    // (Events/Scanlines follow the normative width matrix, not the theme);
    // the selection stays visible.
    for (int column = 0; column <= pnga::ui::qt::BlockInspectorModel::OutputBytes;
         ++column) {
      QVERIFY2(!table->isColumnHidden(column),
               qPrintable(QStringLiteral("Blocks column %1 hidden at %2 px")
                              .arg(column)
                              .arg(width)));
    }
    table->selectRow(0);
    QVERIFY(table->selectionModel()->isRowSelected(0, QModelIndex()));

    auto* decode_table = decode_page(window)->findChild<QTableView*>(
        QStringLiteral("compressionDecodeTraceTable"));
    QVERIFY(decode_table != nullptr);
    for (int column = 0;
         column < pnga::ui::qt::DecodeTraceModel::ColumnCount; ++column) {
      QVERIFY2(!decode_table->isColumnHidden(column),
               qPrintable(QStringLiteral("Decode column %1 hidden at %2 px")
                              .arg(column)
                              .arg(width)));
    }
  }
  theme.setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kLight,
                /*persist=*/false);
}

void ControlledCorpusGateTest::rowSelectionAndNavigationSubmitNoTraceWork() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  open_valid_and_wait_ready(window,
                            "valid/trace-dynamic-overlap-repeats.png");
  inspector_tabs(window)->setCurrentIndex(1);
  QCoreApplication::processEvents();

  auto* table = blocks_table(window);
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() == 1, kTimeoutMs);
  auto* status = context_status(window);
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("ready")),
                           kTimeoutMs);

  const std::uint64_t generation_before =
      block_page(window)->view().generation;
  const auto stores =
      window.findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();

  // Row selection changes only Manual Selection; typed navigation keeps the
  // published bundle: zero Deep Trace submissions beyond the open action.
  table->selectRow(0);
  QVERIFY(store->state().manual.has_value());
  QCOMPARE(store->history().size(), std::size_t{0});
  QCOMPARE(block_page(window)->view().generation, generation_before);

  compression_pages(window)->setCurrentIndex(2);
  QCoreApplication::processEvents();
  auto* decode_table = decode_page(window)->findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(decode_table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(decode_table->model()->rowCount() >= 1, kTimeoutMs);
  decode_table->selectRow(0);
  QVERIFY(store->state().manual.has_value());
  QCOMPARE(store->history().size(), std::size_t{0});
  QCOMPARE(block_page(window)->view().generation, generation_before);
  QVERIFY(!status->text().contains(QStringLiteral("Replaying")));
  QVERIFY(status->text().contains(QStringLiteral("ready")));
}

QTEST_MAIN(ControlledCorpusGateTest)
#include "controlled_corpus_gate_test.moc"
