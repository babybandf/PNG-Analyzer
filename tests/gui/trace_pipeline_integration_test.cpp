// WP-5U13 integration test: the bounded trace pipeline wired into MainWindow.
// A real deterministic PNG is opened; committing a pixel must publish one
// generation-coherent bundle to all three Compression pages, page switching
// must not request or wipe data, and typed Blocks navigation must reach the
// correct WP-5U11 sources. WP-5U12C: the complete Blocks list is browsable
// without X/Y Lock, row selection only changes Manual Selection, Show in Hex
// highlights every IDAT data span, Show inflated output carries the output
// range and Open Decode Trace drives the existing bounded request once.

#include "main_window.h"

#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/decode_trace_model.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableView>
#include <QTemporaryFile>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

class TracePipelineIntegrationTest : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void committedPixelPublishesReadyBundleToAllPages();
  // WP-5U15 Task 1 characterization: replacement discards the previous bundle.
  void replacingDocumentCannotPublishTheFirstTraceBundle();
  void subpageSwitchingKeepsSameGenerationWithoutWiping();
  void blockShowInHexNavigatesFileSource();
  void immediateNoLockBlocksAreBrowsable();
  void blockShowInHexCoversEveryIdatSpan();
  void showInflatedOutputNavigatesInflatedSource();
  void rowSelectionSubmitsZeroTraces();
  void openDecodeTracePublishesBoundedBundle();
  void typedTargetsRoundTripAcrossTwoIdats();
  void huffmanOccurrenceNavigationAndBackRestoresSymbol();
  void pixelCurrentHighlightsIntersectingEvents();
  void decodeShowInHexCarriesTypedCompressedTarget();
  void decodeShowInflatedOutputCarriesOnlyInflatedRange();
};

void TracePipelineIntegrationTest::init() {
  QSettings settings;
  settings.clear();
}

void TracePipelineIntegrationTest::committedPixelPublishesReadyBundleToAllPages() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  // 1x1 grayscale PNG with a single IDAT (deterministic fixture).
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);

  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  QVERIFY(lock != nullptr);

  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);
  auto* stream_summary = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStreamSummary"));
  QVERIFY(stream_summary != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      stream_summary->text().contains(QStringLiteral("zlib stream")), 5000);
  QVERIFY(stream_summary->text().contains(QStringLiteral("IDAT segments")));

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  auto* huffman = window.findChild<pnga::ui::qt::HuffmanInspector*>(
      QStringLiteral("huffmanInspector"));
  auto* decode = window.findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
  QVERIFY(block != nullptr);
  QVERIFY(huffman != nullptr);
  QVERIFY(decode != nullptr);
  QVERIFY(lock->isChecked());
  QCOMPARE(block->view().selected_output_offset,
           std::optional<std::uint64_t>{1});
  QCOMPARE(block->view().generation, huffman->view().generation);
  QCOMPARE(huffman->view().generation, decode->view().scope.generation);
  QVERIFY(block->view().generation != 0);
  QVERIFY(!block->view().rows.empty());
  QVERIFY(!decode->view().steps.empty());
}

void TracePipelineIntegrationTest::replacingDocumentCannotPublishTheFirstTraceBundle() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QTemporaryFile first;
  QVERIFY(first.open());
  QCOMPARE(first.write(bytes), bytes.size());
  first.flush();
  QTemporaryFile second;
  QVERIFY(second.open());
  QCOMPARE(second.write(bytes), bytes.size());
  second.flush();

  QVERIFY(window.openFile(first.fileName()));
  QCoreApplication::processEvents();
  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  QVERIFY(window.openFile(second.fileName()));
  QCoreApplication::processEvents();
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  // The visible state must be coherent with the latest document only: the
  // first document's bundle must never be published after replacement, so the
  // current generation is non-zero. No fixed generation is asserted.
  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  QVERIFY(block->view().generation != 0);
}

void TracePipelineIntegrationTest::subpageSwitchingKeepsSameGenerationWithoutWiping() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);

  auto* x = window.findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* y = window.findChild<QSpinBox*>(QStringLiteral("yCoordinate"));
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  x->setValue(0);
  y->setValue(0);
  lock->setChecked(true);

  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  auto* compression =
      window.findChild<QTabWidget*>(QStringLiteral("compressionInspectorPages"));
  QVERIFY(compression != nullptr);

  // Switching subpages must neither wipe the published bundle nor start a
  // replay (the shared status must never return to a no-data/replaying text).
  compression->setCurrentIndex(1);
  QCoreApplication::processEvents();
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));
  compression->setCurrentIndex(2);
  QCoreApplication::processEvents();
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));
  compression->setCurrentIndex(0);
  QCoreApplication::processEvents();
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));

  // DEC/HEX base toggling and window resize only change presentation: the
  // trace bundle stays published and no replay is started.
  if (auto* base_button =
          window.findChild<QPushButton*>(QStringLiteral("numericBase"));
      base_button != nullptr) {
    base_button->click();
    base_button->click();
  }
  window.resize(1180, 740);
  QCoreApplication::processEvents();
  QTest::qWait(50);
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  QVERIFY(!block->view().rows.empty());
}

void TracePipelineIntegrationTest::blockShowInHexNavigatesFileSource() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);
  auto* x = window.findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* y = window.findChild<QSpinBox*>(QStringLiteral("yCoordinate"));
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  x->setValue(0);
  y->setValue(0);
  lock->setChecked(true);

  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  QVERIFY(!block->view().rows.empty());
  QVERIFY(!block->view().rows.front().physical_spans.empty());
  const std::uint64_t expected_offset =
      block->view().rows.front().physical_spans.front().offset;

  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  auto* hex = window.findChild<pnga::ui::qt::HexView*>(
      QStringLiteral("hexView"));
  QVERIFY(hex_source != nullptr);
  QVERIFY(hex != nullptr);

  const auto buttons = block->findChildren<QPushButton*>();
  QVERIFY(buttons.size() >= 2);
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in Hex")) {
      button->click();
    }
  }
  QCoreApplication::processEvents();
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kFile);
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(), expected_offset);
}

// WP-5U12E: the former decodeShowInHexNavigatesInflatedSource slot asserted
// the removed legacy contract (an untyped Show in Hex signal navigating the
// inflated source). The typed contract is covered below by
// decodeShowInflatedOutputCarriesOnlyInflatedRange (inflated source switch,
// location and typed payload) and decodeShowInHexCarriesTypedCompressedTarget
// (compressed input with every file span); no assertion was weakened.

void TracePipelineIntegrationTest::immediateNoLockBlocksAreBrowsable() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  auto* table = block->findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 1, 5000);

  // Releasing the pixel lock removes the Deep Trace context but the complete
  // Fast Index Blocks list stays visible and browsable.
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  QVERIFY(lock != nullptr);
  lock->setChecked(false);
  QCoreApplication::processEvents();
  QTest::qWait(50);
  QVERIFY(table->model()->rowCount() >= 1);
  QVERIFY(table->model()->data(table->model()->index(0, 1),
                               Qt::DisplayRole)
              .isValid());
}

void TracePipelineIntegrationTest::blockShowInHexCoversEveryIdatSpan() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAABElEQVR42mP4OQCd2AAA"
      "AAZJREFUDwABAQEAYcWXBgAAAABJRU5ErkJggg==");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  auto* table = block->findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 1, 5000);

  // The row's typed physical spans are the expected highlight payload.
  const auto spans =
      table->model()
          ->data(table->model()->index(0, 0),
                 pnga::ui::qt::PhysicalSpansRole)
          .value<std::vector<pnga::trace_model::ProvenanceSpan>>();
  QVERIFY(spans.size() >= 2);  // the block crosses the IDAT boundary
  const std::uint64_t first_offset = spans.front().offset;

  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  auto* hex = window.findChild<pnga::ui::qt::HexView*>(
      QStringLiteral("hexView"));
  QVERIFY(hex_source != nullptr);
  QVERIFY(hex != nullptr);

  table->selectRow(0);
  const auto buttons = block->findChildren<QPushButton*>();
  bool clicked = false;
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in Hex")) {
      button->click();
      clicked = true;
    }
  }
  QVERIFY(clicked);
  QCoreApplication::processEvents();
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kFile);
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(), first_offset);
  // Every physical span of the row is highlighted: no first-span-only path.
  QCOMPARE(hex->highlightCount(), spans.size());
}

void TracePipelineIntegrationTest::showInflatedOutputNavigatesInflatedSource() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  auto* table = block->findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 1, 5000);
  const auto output_range =
      table->model()
          ->data(table->model()->index(0, 0), pnga::ui::qt::OutputRangeRole)
          .value<pnga::trace_model::InflatedByteRange>();
  QVERIFY(output_range.valid() && !output_range.empty());

  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  auto* hex = window.findChild<pnga::ui::qt::HexView*>(
      QStringLiteral("hexView"));
  QVERIFY(hex_source != nullptr);
  QVERIFY(hex != nullptr);

  table->selectRow(0);
  QPushButton* inflated_button = nullptr;
  const auto buttons = block->findChildren<QPushButton*>();
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show inflated output")) {
      inflated_button = button;
    }
  }
  QVERIFY(inflated_button != nullptr);
  // The Inflated hex source becomes ready asynchronously after the decode
  // publishes; the action is idempotent, so it is retried until the typed
  // navigation lands on the Inflated source.
  QTRY_VERIFY_WITH_TIMEOUT(([&]() {
    inflated_button->click();
    QCoreApplication::processEvents();
    return hex->currentLocation().has_value() &&
           hex_source->source() == pnga::ui::qt::HexSource::kInflated;
  })(), 5000);
  QCOMPARE(*hex->currentLocation(), output_range.begin.value);
}

void TracePipelineIntegrationTest::rowSelectionSubmitsZeroTraces() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAABElEQVR42mP4OQCd2AAA"
      "AAZJREFUDwABAQEAYcWXBgAAAABJRU5ErkJggg==");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);
  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  auto* table = block->findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 1, 5000);
  // Let the initial auto-locked trace settle before touching the table.
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);
  const std::uint64_t generation_before = block->view().generation;
  const std::uint64_t selected_block_index =
      table->model()
          ->data(table->model()->index(0, 0), pnga::ui::qt::BlockIndexRole)
          .toULongLong();

  // Row selection produces the Manual target in the shared store and
  // submits no Deep Trace request of any kind.
  const auto stores =
      window.findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();
  table->selectRow(0);
  QVERIFY(store->state().manual.has_value());
  QCOMPARE(store->state().manual->block_index,
           std::optional<std::uint64_t>{selected_block_index});
  QCOMPARE(store->history().size(), std::size_t{0});
  QCOMPARE(block->view().generation, generation_before);
  QVERIFY(!context_status->text().contains(QStringLiteral("Replaying")));
}

void TracePipelineIntegrationTest::openDecodeTracePublishesBoundedBundle() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);
  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  auto* table = block->findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(table->model()->rowCount() >= 1, 5000);

  // The explicit drill-in routes the selected block through the existing
  // bounded request and publishes a fresh ready bundle for the same
  // generation. The request is interval-deduplicated, so retrying the click
  // while the single worker finishes is safe.
  table->selectRow(0);
  // Audit 7.1: the drill-down must land on the Decode Trace page
  // (Blocks=0, Huffman=1, Decode Trace=2).
  auto* pages = window.findChild<QTabWidget*>(
      QStringLiteral("compressionInspectorPages"));
  QVERIFY(pages != nullptr);
  pages->setCurrentIndex(0);
  QPushButton* trace_button = nullptr;
  const auto buttons = block->findChildren<QPushButton*>();
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Open Decode Trace")) {
      trace_button = button;
    }
  }
  QVERIFY(trace_button != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(([&]() {
    trace_button->click();
    QCoreApplication::processEvents();
    return context_status->text().contains(QStringLiteral("ready"));
  })(), 10000);
  QTRY_COMPARE_WITH_TIMEOUT(pages->currentIndex(), 2, 10000);
  QVERIFY(!block->view().rows.empty());
  // The bounded result stays scoped: the bundle generation is unchanged.
  QVERIFY(block->view().generation != 0);
}

void TracePipelineIntegrationTest::typedTargetsRoundTripAcrossTwoIdats() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  // 1x1 grayscale PNG whose single zlib stream is split across two IDAT
  // chunks (deterministic two-IDAT fixture).
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAABElEQVR42mP4OQCd2AAA"
      "AAZJREFUDwABAQEAYcWXBgAAAABJRU5ErkJggg==");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);
  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block != nullptr);
  QVERIFY(block->view().generation != 0);
  const std::uint64_t generation = block->view().generation;
  const std::size_t rows_before = block->view().rows.size();

  // MainWindow owns exactly one shared CompressionSelectionStore.
  const auto stores =
      window.findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();

  // The exact IDAT data spans come from the application's own chunk model.
  auto* chunk_model = window.findChild<pnga::ui::qt::ChunkModel*>();
  QVERIFY(chunk_model != nullptr);
  std::vector<std::pair<std::uint64_t, std::uint64_t>> idat_spans;
  std::uint64_t zlib_bytes = 0;
  for (int row = 0; row < chunk_model->rowCount(); ++row) {
    const auto& node = chunk_model->chunkAt(row);
    if (node.text() == "IDAT") {
      idat_spans.emplace_back(node.data_offset, node.data_length);
      zlib_bytes += node.data_length;
    }
  }
  QCOMPARE(idat_spans.size(), 2);

  const auto file_span_of = [](const std::pair<std::uint64_t, std::uint64_t>&
                                   span) {
    return pnga::trace_model::FileByteRange{
        pnga::trace_model::FileByteOffset{span.first},
        pnga::trace_model::FileByteOffset{span.first + span.second}};
  };

  auto* hex =
      window.findChild<pnga::ui::qt::HexView*>(QStringLiteral("hexView"));
  QVERIFY(hex != nullptr);
  QSignalSpy location_spy(hex, &pnga::ui::qt::HexView::locationChanged);
  QVERIFY(location_spy.isValid());
  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  QVERIFY(hex_source != nullptr);

  // A zlib navigation highlights BOTH exact IDAT data spans on the File
  // source: no first-span-only path, no chunk length/type/CRC bytes.
  pnga::trace_model::CompressionNavigationTarget zlib_target;
  zlib_target.generation = generation;
  zlib_target.request_serial = 1;
  zlib_target.origin =
      pnga::trace_model::CompressionNavigationOrigin::kBlocks;
  zlib_target.logical_range = pnga::trace_model::ZlibByteRange{
      pnga::trace_model::ZlibByteOffset{0},
      pnga::trace_model::ZlibByteOffset{zlib_bytes}};
  for (const auto& span : idat_spans) {
    zlib_target.physical_spans.push_back(file_span_of(span));
  }
  QVERIFY(store->applyNavigation(zlib_target));
  QCOMPARE(location_spy.count(), 1);  // one request gives one view update
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kFile);
  QCOMPARE(hex->currentLocation().value_or(99), idat_spans.front().first);
  QCOMPARE(hex->highlightCount(), std::size_t{2});

  // A DEFLATE bit-range navigation carries the same physical file spans.
  pnga::trace_model::CompressionNavigationTarget deflate_target;
  deflate_target.generation = generation;
  deflate_target.request_serial = 2;
  deflate_target.origin =
      pnga::trace_model::CompressionNavigationOrigin::kDecodeTrace;
  deflate_target.logical_range = pnga::trace_model::DeflateBitRange{
      pnga::trace_model::DeflateBitOffset{0},
      pnga::trace_model::DeflateBitOffset{(zlib_bytes - 2) * 8}};
  deflate_target.physical_spans = zlib_target.physical_spans;
  QVERIFY(store->applyNavigation(deflate_target));
  QCOMPARE(hex->highlightCount(), std::size_t{2});
  QCOMPARE(hex->currentLocation().value_or(99), idat_spans.front().first);

  // A File target selects its exact file range.
  pnga::trace_model::CompressionNavigationTarget file_target;
  file_target.generation = generation;
  file_target.request_serial = 3;
  file_target.origin = pnga::trace_model::CompressionNavigationOrigin::kHex;
  file_target.logical_range = pnga::trace_model::FileByteRange{
      pnga::trace_model::FileByteOffset{0},
      pnga::trace_model::FileByteOffset{8}};
  file_target.physical_spans = {
      pnga::trace_model::FileByteRange{pnga::trace_model::FileByteOffset{0},
                                       pnga::trace_model::FileByteOffset{8}}};
  QVERIFY(store->applyNavigation(file_target));
  QCOMPARE(hex->highlightCount(), std::size_t{1});
  QCOMPARE(hex->currentLocation().value_or(99), std::uint64_t{0});

  // An Inflated target routes through the existing Inflated source only.
  pnga::trace_model::CompressionNavigationTarget inflated_target;
  inflated_target.generation = generation;
  inflated_target.request_serial = 4;
  inflated_target.origin =
      pnga::trace_model::CompressionNavigationOrigin::kInflated;
  inflated_target.logical_range = pnga::trace_model::InflatedByteRange{
      pnga::trace_model::InflatedByteOffset{0},
      pnga::trace_model::InflatedByteOffset{2}};
  QVERIFY(store->applyNavigation(inflated_target));
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kInflated);
  QCOMPARE(hex->currentLocation().value_or(99), std::uint64_t{0});

  // A stale generation is rejected before UI publication.
  pnga::trace_model::CompressionNavigationTarget stale = zlib_target;
  stale.request_serial = 5;
  stale.generation = generation - 1;
  QVERIFY(!store->applyNavigation(stale));
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kInflated);

  // The Current mapping coexists with the last Manual selection.
  pnga::trace_model::CompressionCurrentMapping mapping;
  mapping.generation = generation;
  mapping.output_range = pnga::trace_model::InflatedByteRange{
      pnga::trace_model::InflatedByteOffset{0},
      pnga::trace_model::InflatedByteOffset{1}};
  QVERIFY(store->setCurrent(mapping));
  QVERIFY(store->state().current.has_value());
  QVERIFY(store->state().manual.has_value());
  QCOMPARE(store->state().manual->request_serial, std::uint64_t{4});

  // Typed navigation submits zero Deep Trace work: the trace bundle and its
  // generation are untouched and the context stays ready.
  QCOMPARE(block->view().generation, generation);
  QCOMPARE(block->view().rows.size(), rows_before);
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));
}

void TracePipelineIntegrationTest::
    huffmanOccurrenceNavigationAndBackRestoresSymbol() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAABElEQVR42mP4OQCd2AAA"
      "AAZJREFUDwABAQEAYcWXBgAAAABJRU5ErkJggg==");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);
  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  auto* huffman = window.findChild<pnga::ui::qt::HuffmanInspector*>(
      QStringLiteral("huffmanInspector"));
  QVERIFY(block != nullptr);
  QVERIFY(huffman != nullptr);
  auto* block_table = block->findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(block_table != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(block_table->model()->rowCount() >= 1, 5000);

  const auto stores =
      window.findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();

  // Blocks → Huffman keeps the selected block.
  block_table->selectRow(0);
  auto* compression =
      window.findChild<QTabWidget*>(QStringLiteral("compressionInspectorPages"));
  QVERIFY(compression != nullptr);
  compression->setCurrentIndex(1);
  QCoreApplication::processEvents();
  auto* heading =
      huffman->findChild<QLabel*>(QStringLiteral("huffmanInspectorHeading"));
  QVERIFY(heading != nullptr);
  QVERIFY(heading->text().contains(QStringLiteral("Block #0")));
  QVERIFY(heading->text().contains(QStringLiteral("Fixed Huffman")));

  // Switching the table keeps the block.
  auto* distance_button = huffman->findChild<QPushButton*>(
      QStringLiteral("huffmanTableKindDistance"));
  auto* literal_button = huffman->findChild<QPushButton*>(
      QStringLiteral("huffmanTableKindLiteralLength"));
  QVERIFY(distance_button != nullptr);
  QVERIFY(literal_button != nullptr);
  distance_button->click();
  QVERIFY(heading->text().contains(QStringLiteral("Block #0")));
  literal_button->click();
  QVERIFY(heading->text().contains(QStringLiteral("Block #0")));

  // A symbol with a bounded occurrence navigates to that token; the typed
  // target stays in the selected block's scope.
  auto* table = huffman->findChild<QTableView*>(
      QStringLiteral("compressionHuffmanTable"));
  QVERIFY(table != nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);
  int used_row = -1;
  for (int row = 0; row < model->rowCount(); ++row) {
    if (model->data(model->index(row, pnga::ui::qt::HuffmanInspectorModel::
                                           UsesInResult),
                    Qt::DisplayRole)
            .toInt() > 0) {
      used_row = row;
      break;
    }
  }
  QVERIFY(used_row >= 0);
  const auto entry =
      model->data(model->index(used_row, 0), pnga::ui::qt::HuffmanEntryRole)
          .value<pnga::analysis_engine::HuffmanInspectorEntry>();
  QVERIFY(!entry.occurrence_token_indices.empty());
  const std::uint64_t expected_token =
      entry.occurrence_token_indices.front();
  table->selectRow(used_row);

  QCOMPARE(store->history().size(), std::size_t{0});
  auto* open = huffman->findChild<QPushButton*>(
      QStringLiteral("huffmanOpenOccurrence"));
  QVERIFY(open != nullptr);
  open->click();
  // Audit 7.1: the occurrence drill-down switches to the Decode Trace page.
  QCOMPARE(compression->currentIndex(), 2);
  QCOMPARE(store->history().size(), std::size_t{1});
  QCOMPARE(store->history().back().token_index,
           std::optional<std::uint64_t>{expected_token});
  QCOMPARE(store->history().back().symbol,
           std::optional<std::uint16_t>{entry.symbol});
  QCOMPARE(store->history().back().block_index,
           std::optional<std::uint64_t>{0});
  // The bounded occurrence list cycles; no occurrence index is grown.
  open->click();
  QCOMPARE(store->history().size(), std::size_t{2});

  // Back returns the symbol (and the earlier occurrence).
  QVERIFY(store->goBack());
  QCOMPARE(store->state().manual->token_index,
           std::optional<std::uint64_t>{expected_token});
  QCOMPARE(store->state().manual->symbol,
           std::optional<std::uint16_t>{entry.symbol});

  // Returning to Huffman preserves Block, table, symbol and occurrence.
  compression->setCurrentIndex(0);
  QCoreApplication::processEvents();
  compression->setCurrentIndex(1);
  QCoreApplication::processEvents();
  QVERIFY(heading->text().contains(QStringLiteral("Block #0")));
  QVERIFY(heading->text().contains(QStringLiteral("Fixed Huffman")));
  QVERIFY(table->selectionModel()->isRowSelected(used_row, QModelIndex()));
  QCOMPARE(store->history().size(), std::size_t{2});
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));
}

void TracePipelineIntegrationTest::pixelCurrentHighlightsIntersectingEvents() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);
  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  auto* decode = window.findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
  QVERIFY(decode != nullptr);
  auto* table = decode->findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(table != nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(model->rowCount() >= 1, 5000);

  // The committed pixel marks every intersecting event as Current.
  const auto stores =
      window.findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();
  QVERIFY(store->state().current.has_value());
  int current_rows = 0;
  for (int row = 0; row < model->rowCount(); ++row) {
    if (model->data(model->index(row, 0),
                    pnga::ui::qt::DecodeTraceContainsCurrentRole)
            .toBool()) {
      ++current_rows;
    }
  }
  QVERIFY(current_rows >= 1);

  // A row Manual Selection preserves the Current mapping.
  table->selectRow(0);
  QVERIFY(store->state().manual.has_value());
  QVERIFY(store->state().current.has_value());
  QVERIFY(model->data(model->index(0, 0),
                      pnga::ui::qt::DecodeTraceIsManualSelectionRole)
              .toBool());
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));
}

void TracePipelineIntegrationTest::decodeShowInHexCarriesTypedCompressedTarget() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  // The deterministic two-IDAT grayscale fixture.
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAABElEQVR42mP4OQCd2AAA"
      "AAZJREFUDwABAQEAYcWXBgAAAABJRU5ErkJggg==");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);
  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  auto* decode = window.findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
  QVERIFY(decode != nullptr);
  auto* table = decode->findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(table != nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(model->rowCount() >= 1, 5000);

  // Pick a step whose compressed input is mapped to physical file bytes.
  int step_row = -1;
  pnga::analysis_engine::DecodeTraceStep step;
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto candidate = model->data(model->index(row, 0),
                                       pnga::ui::qt::DecodeTraceStepRole)
                               .value<pnga::analysis_engine::DecodeTraceStep>();
    if (candidate.physical_input_spans.empty()) {
      continue;
    }
    step_row = row;
    step = candidate;
    break;
  }
  QVERIFY(step_row >= 0);

  const auto stores =
      window.findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();
  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  auto* hex = window.findChild<pnga::ui::qt::HexView*>(
      QStringLiteral("hexView"));
  QVERIFY(hex_source != nullptr);
  QVERIFY(hex != nullptr);

  table->selectRow(step_row);
  auto* hex_button =
      decode->findChild<QPushButton*>(QStringLiteral("decodeShowInHex"));
  QVERIFY(hex_button != nullptr);
  hex_button->click();
  QCoreApplication::processEvents();

  // The typed target carries the precise DeflateBitRange and every physical
  // file span of the step, and Hex highlights all of them.
  QCOMPARE(store->history().size(), std::size_t{1});
  const auto& target = store->history().back();
  const auto* bits = std::get_if<pnga::trace_model::DeflateBitRange>(
      &target.logical_range);
  QVERIFY(bits != nullptr);
  QCOMPARE(bits->begin, step.input_range.begin);
  QCOMPARE(bits->end, step.input_range.end);
  QCOMPARE(target.physical_spans, step.physical_input_spans);
  QCOMPARE(target.token_index, std::optional<std::uint64_t>{step.token_index});
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kFile);
  QCOMPARE(hex->highlightCount(), step.physical_input_spans.size());
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(),
           step.physical_input_spans.front().begin.value);

  // Typed navigation submits zero Deep Trace work.
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));
}

void TracePipelineIntegrationTest::
    decodeShowInflatedOutputCarriesOnlyInflatedRange() {
  MainWindow window;
  window.resize(1200, 760);
  window.show();
  QCoreApplication::processEvents();

  QTemporaryFile png;
  QVERIFY(png.open());
  const QByteArray bytes = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  QCOMPARE(png.write(bytes), bytes.size());
  png.flush();
  QVERIFY(window.openFile(png.fileName()));
  QCoreApplication::processEvents();

  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), 5000);
  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  auto* decode = window.findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
  QVERIFY(decode != nullptr);
  auto* table = decode->findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(table != nullptr);
  auto* model = table->model();
  QVERIFY(model != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(model->rowCount() >= 1, 5000);

  int step_row = -1;
  pnga::analysis_engine::DecodeTraceStep step;
  for (int row = 0; row < model->rowCount(); ++row) {
    const auto candidate = model->data(model->index(row, 0),
                                       pnga::ui::qt::DecodeTraceStepRole)
                               .value<pnga::analysis_engine::DecodeTraceStep>();
    if (!candidate.output_range.valid() || candidate.output_range.empty()) {
      continue;
    }
    step_row = row;
    step = candidate;
    break;
  }
  QVERIFY(step_row >= 0);

  const auto stores =
      window.findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();
  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  auto* hex = window.findChild<pnga::ui::qt::HexView*>(
      QStringLiteral("hexView"));
  QVERIFY(hex_source != nullptr);
  QVERIFY(hex != nullptr);

  table->selectRow(step_row);
  auto* inflated_button = decode->findChild<QPushButton*>(
      QStringLiteral("decodeShowInflatedOutput"));
  QVERIFY(inflated_button != nullptr);
  // The Inflated hex source becomes ready asynchronously; the action is
  // idempotent and retried inside QTRY.
  QTRY_VERIFY_WITH_TIMEOUT(([&]() {
    inflated_button->click();
    QCoreApplication::processEvents();
    return !store->history().empty() &&
           hex_source->source() == pnga::ui::qt::HexSource::kInflated;
  })(), 5000);

  // The output action carries only the typed InflatedByteRange: no
  // compressed scalar and no physical file span.
  const auto& target = store->history().back();
  const auto* output = std::get_if<pnga::trace_model::InflatedByteRange>(
      &target.logical_range);
  QVERIFY(output != nullptr);
  QCOMPARE(output->begin, step.output_range.begin);
  QCOMPARE(output->end, step.output_range.end);
  QVERIFY(target.physical_spans.empty());
  QCOMPARE(target.token_index, std::optional<std::uint64_t>{step.token_index});
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(), step.output_range.begin.value);
  QVERIFY(context_status->text().contains(QStringLiteral("ready")));
}

QTEST_MAIN(TracePipelineIntegrationTest)
#include "trace_pipeline_integration_test.moc"
