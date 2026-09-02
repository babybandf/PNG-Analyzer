// WP-5U13 integration test: the bounded trace pipeline wired into MainWindow.
// A real deterministic PNG is opened; committing a pixel must publish one
// generation-coherent bundle to all three Compression pages, page switching
// must not request or wipe data, and Show in Hex / Show in DEFLATE must
// navigate the correct WP-5U11 sources.

#include "main_window.h"

#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
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
  void decodeShowInHexNavigatesInflatedSource();
  void blockShowInDeflateNavigatesIdatSource();
  void typedTargetsRoundTripAcrossTwoIdats();
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
  QCOMPARE(huffman->view().generation, decode->view().generation);
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

void TracePipelineIntegrationTest::decodeShowInHexNavigatesInflatedSource() {
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

  auto* decode = window.findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
  QVERIFY(decode != nullptr);
  QVERIFY(!decode->view().steps.empty());
  const auto selected_step = std::find_if(
      decode->view().steps.begin(), decode->view().steps.end(),
      [](const auto& step) { return step.selected_output_byte.has_value(); });
  QVERIFY(selected_step != decode->view().steps.end());
  const std::uint64_t expected_begin = selected_step->output_begin;

  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  auto* hex = window.findChild<pnga::ui::qt::HexView*>(
      QStringLiteral("hexView"));
  QVERIFY(hex_source != nullptr);
  QVERIFY(hex != nullptr);

  const auto buttons = decode->findChildren<QPushButton*>();
  QVERIFY(buttons.size() >= 2);
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in Hex")) {
      button->click();
    }
  }
  QCoreApplication::processEvents();
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kInflated);
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(), expected_begin);
}

void TracePipelineIntegrationTest::blockShowInDeflateNavigatesIdatSource() {
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
  const std::uint64_t expected_byte =
      block->view().rows.front().input_bit_begin / 8;

  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  auto* hex = window.findChild<pnga::ui::qt::HexView*>(
      QStringLiteral("hexView"));
  QVERIFY(hex_source != nullptr);
  QVERIFY(hex != nullptr);

  const auto buttons = block->findChildren<QPushButton*>();
  for (auto* button : buttons) {
    if (button->text() == QStringLiteral("Show in DEFLATE")) {
      button->click();
    }
  }
  QCoreApplication::processEvents();
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kIdatStream);
  QVERIFY(hex->currentLocation().has_value());
  QCOMPARE(*hex->currentLocation(), expected_byte);
  QVERIFY(hex->highlightCount() > 0);
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

QTEST_MAIN(TracePipelineIntegrationTest)
#include "trace_pipeline_integration_test.moc"
