// WP-5U13 integration test: the bounded trace pipeline wired into MainWindow.
// A real deterministic PNG is opened; committing a pixel must publish one
// generation-coherent bundle to all three Compression pages, page switching
// must not request or wipe data, and Show in Hex / Show in DEFLATE must
// navigate the correct WP-5U11 sources.

#include "main_window.h"

#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryFile>

class TracePipelineIntegrationTest : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void committedPixelPublishesReadyBundleToAllPages();
  void subpageSwitchingKeepsSameGenerationWithoutWiping();
  void blockShowInHexNavigatesFileSource();
  void decodeShowInHexNavigatesInflatedSource();
  void blockShowInDeflateNavigatesIdatSource();
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

  auto* x = window.findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* y = window.findChild<QSpinBox*>(QStringLiteral("yCoordinate"));
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  QVERIFY(x != nullptr);
  QVERIFY(y != nullptr);
  QVERIFY(lock != nullptr);
  x->setValue(0);
  y->setValue(0);
  lock->setChecked(true);
  QCoreApplication::processEvents();

  auto* context_status = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(
      context_status->text().contains(QStringLiteral("ready")), 5000);

  auto* block = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  auto* huffman = window.findChild<pnga::ui::qt::HuffmanInspector*>(
      QStringLiteral("huffmanInspector"));
  auto* decode = window.findChild<pnga::ui::qt::DecodeTraceInspector*>(
      QStringLiteral("decodeTraceInspector"));
  QVERIFY(block != nullptr);
  QVERIFY(huffman != nullptr);
  QVERIFY(decode != nullptr);
  QCOMPARE(block->view().generation, huffman->view().generation);
  QCOMPARE(huffman->view().generation, decode->view().generation);
  QVERIFY(block->view().generation != 0);
  QVERIFY(!block->view().rows.empty());
  QVERIFY(!decode->view().steps.empty());
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
  const std::uint64_t expected_begin = decode->view().steps.front().output_begin;

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
}

QTEST_MAIN(TracePipelineIntegrationTest)
#include "trace_pipeline_integration_test.moc"
