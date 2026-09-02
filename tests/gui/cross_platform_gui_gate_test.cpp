// WP-5U6C: deterministic GUI gate for layout, DPI/theme resilience,
// shortcuts, focus order and basic accessibility metadata. The test never
// opens a file or invokes a decoder.

#include "main_window.h"

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/ui/qt/about_dialog.h>
#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFontMetrics>
#include <QKeySequence>
#include <QMenuBar>
#include <QPalette>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QSplitter>
#include <QSysInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QtGlobal>

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

bool focus_chain_reaches(QWidget* start, QWidget* target) {
  if (start == nullptr || target == nullptr) {
    return false;
  }
  QWidget* current = start;
  for (int step = 0; step < 256; ++step) {
    current = current->nextInFocusChain();
    if (current == target) {
      return true;
    }
    if (current == start) {
      return false;
    }
  }
  return false;
}

template <typename Widget>
void verify_bounded_table(Widget& widget, int expected_rows) {
  auto* table = widget.template findChild<QTableWidget*>();
  QVERIFY(table != nullptr);
  QCOMPARE(table->rowCount(), expected_rows);
  QVERIFY(table->item(expected_rows - 1, 1) != nullptr);
  QCOMPARE(table->item(expected_rows - 1, 1)->text(),
           QStringLiteral("truncated"));
}

}  // namespace

class CrossPlatformGuiGateTest : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase();
  void layoutSurvivesReferenceSizesAndDpi();
  void themeSwitchKeepsInspectorVisible();
  void themeMenuAppliesWithoutResettingLayout();
  void shortcutsAndFocusOrderAreStable();
  void accessibilityNamesCoverControlsAndInspectors();
  void inspectorTruncationContractsRemainBounded();
};

void CrossPlatformGuiGateTest::initTestCase() {
  QApplication::setWindowIcon(pnga::ui::qt::application_icon());
  QVERIFY(!QApplication::windowIcon().isNull());
  MainWindow icon_probe;
  QVERIFY(!icon_probe.windowIcon().isNull());
  const auto* screen = QGuiApplication::primaryScreen();
  QVERIFY(screen != nullptr);
  qInfo() << "GUI gate platform=" << QSysInfo::prettyProductName()
          << "kernel=" << QSysInfo::kernelType()
          << QSysInfo::currentCpuArchitecture() << "qt=" << qVersion()
          << "screen_dpr=" << screen->devicePixelRatio()
          << "logical_dpi=" << screen->logicalDotsPerInch();
  QVERIFY(screen->devicePixelRatio() > 0.0);
  QVERIFY(screen->logicalDotsPerInch() > 0.0);
  const QByteArray requested_scale = qgetenv("QT_SCALE_FACTOR");
  if (!requested_scale.isEmpty()) {
    bool ok = false;
    const qreal expected_scale = requested_scale.toDouble(&ok);
    QVERIFY(ok);
    QVERIFY(qAbs(screen->devicePixelRatio() - expected_scale) < 0.01);
  }
}

void CrossPlatformGuiGateTest::layoutSurvivesReferenceSizesAndDpi() {
  MainWindow window;
  auto* splitter = window.findChild<QSplitter*>(
      QStringLiteral("previewHexSplitter"));
  auto* inspector = window.findChild<QTabWidget*>(
      QStringLiteral("inspectorTabs"));
  auto* preview = window.findChild<QTabWidget*>(
      QStringLiteral("previewTabs"));
  QVERIFY(splitter != nullptr);
  QVERIFY(inspector != nullptr);
  QVERIFY(preview != nullptr);
  QCOMPARE(inspector->count(), 2);
  QCOMPARE(inspector->tabText(0), QStringLiteral("Reconstruction"));
  QCOMPARE(inspector->tabText(1), QStringLiteral("Compression"));

  for (const QSize size : {QSize(900, 600), QSize(1600, 1000)}) {
    window.resize(size);
    window.show();
    QCoreApplication::processEvents();
    QVERIFY(window.width() >= 900);
    QVERIFY(window.height() >= 600);
    QVERIFY(splitter->sizes().size() == 2);
    QVERIFY(splitter->sizes().at(0) > 0);
    QVERIFY(splitter->sizes().at(1) > 0);
    QVERIFY(preview->tabBar()->isVisible());
    QVERIFY(inspector->tabBar()->isVisible());
    QVERIFY(inspector->tabBar()->height() > 0);
  }

  auto* x = window.findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* base = window.findChild<QPushButton*>(QStringLiteral("numericBase"));
  QVERIFY(x != nullptr);
  QVERIFY(base != nullptr);
  QVERIFY(x->sizeHint().height() > 0);
  QVERIFY(base->sizeHint().height() > 0);
  QVERIFY(QFontMetrics(window.font()).height() > 0);
  QVERIFY(window.findChild<QWidget*>(QStringLiteral("coordinateToolbar")) !=
          nullptr);
  QVERIFY(window.findChild<QWidget*>(QStringLiteral("hexView")) != nullptr);
}

void CrossPlatformGuiGateTest::themeSwitchKeepsInspectorVisible() {
  MainWindow window;
  auto* inspector = window.findChild<QTabWidget*>(
      QStringLiteral("inspectorTabs"));
  QVERIFY(inspector != nullptr);
  window.show();
  QCoreApplication::processEvents();

  const QPalette original = QApplication::palette();
  QPalette dark;
  dark.setColor(QPalette::Window, QColor(QStringLiteral("#202124")));
  dark.setColor(QPalette::WindowText, QColor(QStringLiteral("#f5f5f5")));
  dark.setColor(QPalette::Base, QColor(QStringLiteral("#101114")));
  dark.setColor(QPalette::Text, QColor(QStringLiteral("#f5f5f5")));
  QApplication::setPalette(dark);
  QCoreApplication::processEvents();
  QCOMPARE(QApplication::palette().color(QPalette::Window),
           QColor(QStringLiteral("#202124")));
  QVERIFY(inspector->isVisible());
  QVERIFY(inspector->tabBar()->isVisible());

  QPalette light = original;
  light.setColor(QPalette::Window, QColor(Qt::white));
  light.setColor(QPalette::WindowText, QColor(Qt::black));
  QApplication::setPalette(light);
  QCoreApplication::processEvents();
  QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(Qt::white));
  QVERIFY(inspector->isVisible());
  QApplication::setPalette(original);
  QCoreApplication::processEvents();
}

void CrossPlatformGuiGateTest::themeMenuAppliesWithoutResettingLayout() {
  pnga::ui::qt::ApplicationTheme theme(qApp);
  QVERIFY(theme.setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kLight,
                        false));
  MainWindow window(nullptr, &theme);
  QMenu* theme_menu = window.findChild<QMenu*>(QStringLiteral("themeMenu"));
  QVERIFY(theme_menu != nullptr);
  QAction* dark = theme_menu->findChild<QAction*>(QStringLiteral("themeDark"));
  QVERIFY(dark != nullptr);
  QVERIFY(!dark->isChecked());
  dark->trigger();
  QCOMPARE(theme.requestedMode(),
           pnga::ui::qt::ApplicationTheme::ThemeMode::kDark);
  QCOMPARE(theme.effectiveMode(),
           pnga::ui::qt::ApplicationTheme::ThemeMode::kDark);
  QVERIFY(dark->isChecked());
  auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  QVERIFY(preview != nullptr);
  preview->setCurrentIndex(2);
  QMetaObject::invokeMethod(&window, "resetLayout", Qt::DirectConnection);
  QCOMPARE(theme.requestedMode(),
           pnga::ui::qt::ApplicationTheme::ThemeMode::kDark);
  QCOMPARE(preview->currentIndex(), 0);
}

void CrossPlatformGuiGateTest::shortcutsAndFocusOrderAreStable() {
  MainWindow window;
  auto* x = window.findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* y = window.findChild<QSpinBox*>(QStringLiteral("yCoordinate"));
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  auto* base = window.findChild<QPushButton*>(QStringLiteral("numericBase"));
  auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  auto* source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  auto* hex = window.findChild<QWidget*>(QStringLiteral("hexView"));
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(x != nullptr);
  QVERIFY(y != nullptr);
  QVERIFY(lock != nullptr);
  QVERIFY(base != nullptr);
  QVERIFY(preview != nullptr);
  QVERIFY(source != nullptr);
  QVERIFY(hex != nullptr);
  QVERIFY(tabs != nullptr);
  QVERIFY(focus_chain_reaches(x, y));
  QVERIFY(focus_chain_reaches(y, lock));
  QVERIFY(focus_chain_reaches(lock, base));
  QVERIFY(focus_chain_reaches(base, preview));
  QVERIFY(focus_chain_reaches(preview, source));
  QVERIFY(focus_chain_reaches(source, hex));
  QVERIFY(focus_chain_reaches(hex, tabs));

  QAction* open = nullptr;
  for (QAction* menu_action : window.menuBar()->actions()) {
    if (menu_action->menu() == nullptr) {
      continue;
    }
    for (QAction* action : menu_action->menu()->actions()) {
      if (action->text() == QStringLiteral("&Open...")) {
        open = action;
      }
    }
  }
  QVERIFY(open != nullptr);
  QCOMPARE(open->shortcut(), QKeySequence::Open);
}

void CrossPlatformGuiGateTest::accessibilityNamesCoverControlsAndInspectors() {
  MainWindow window;
  const auto check = [&window](const char* object_name) {
    auto* widget = window.findChild<QWidget*>(QString::fromLatin1(object_name));
    QVERIFY(widget != nullptr);
    QVERIFY(!widget->accessibleName().trimmed().isEmpty());
  };
  check("xCoordinate");
  check("yCoordinate");
  check("lockCoordinate");
  check("numericBase");
  check("hexSourceTabs");
  check("inspectorTabs");
  check("compressionInspectorPages");
  check("reconstructInspector");
  check("blockInspector");
  check("huffmanInspector");
  check("decodeTraceInspector");
  check("validationStatus");
  check("previewTabs");
  check("coordinateToolbar");
  check("hexPanel");
  check("hexView");
  auto* inspector = window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(inspector != nullptr);
  for (int i = 0; i < inspector->count(); ++i) {
    QVERIFY(!inspector->tabText(i).trimmed().isEmpty());
    QVERIFY(!inspector->widget(i)->accessibleName().trimmed().isEmpty());
  }
}

void CrossPlatformGuiGateTest::inspectorTruncationContractsRemainBounded() {
  // WP-5U12C mechanism migration: the Blocks page exposes the complete block
  // list through BlockInspectorModel, so the truncation contract is asserted
  // as the virtualized model row count equal to the source fact, with the
  // last row being a real block instead of a "truncated" placeholder. The
  // Huffman and Decode Trace sections are unchanged.
  pnga::analysis_engine::FastCompressionIndexView fast_blocks;
  fast_blocks.status =
      pnga::analysis_engine::FastCompressionIndexStatus::kReady;
  fast_blocks.generation = 1;
  for (int i = 0; i < pnga::ui::qt::BlockInspector::kMaxVisibleRows + 1; ++i) {
    pnga::analysis_engine::FastCompressionBlockRow row;
    row.block_index = static_cast<std::uint64_t>(i);
    row.output_range = {
        pnga::trace_model::InflatedByteOffset{static_cast<std::uint64_t>(i)},
        pnga::trace_model::InflatedByteOffset{static_cast<std::uint64_t>(i) +
                                              1}};
    fast_blocks.blocks.push_back(row);
  }
  pnga::ui::qt::BlockInspector block_widget;
  block_widget.setFastIndex(fast_blocks);
  auto* blocks_table = block_widget.findChild<QTableView*>(
      QStringLiteral("compressionBlocksTable"));
  QVERIFY(blocks_table != nullptr);
  QVERIFY(blocks_table->model() != nullptr);
  QCOMPARE(blocks_table->model()->rowCount(),
           pnga::ui::qt::BlockInspector::kMaxVisibleRows + 1);
  QCOMPARE(blocks_table->model()
               ->data(blocks_table->model()->index(
                          pnga::ui::qt::BlockInspector::kMaxVisibleRows, 1),
                      Qt::DisplayRole)
               .toString(),
           QString::number(static_cast<qulonglong>(
               pnga::ui::qt::BlockInspector::kMaxVisibleRows)));

  pnga::analysis_engine::HuffmanInspectorView huffman;
  pnga::analysis_engine::HuffmanInspectorTable table;
  table.kind = pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  // WP-5U12D mechanism migration: the entry projection gained a meaning and
  // a typed provenance range, so the former positional aggregate init
  // {static_cast<std::uint16_t>(i), 1, 0, 0, 1, false} becomes the field
  // assignments below with the identical values (symbol i, 1 bit, canonical
  // 0, provenance [0, 1), unselected).
  for (int i = 0; i < pnga::ui::qt::HuffmanInspector::kMaxVisibleRows + 1;
       ++i) {
    pnga::analysis_engine::HuffmanInspectorEntry entry;
    entry.symbol = static_cast<std::uint16_t>(i);
    entry.meaning = "literal " + std::to_string(i);
    entry.bit_length = 1;
    entry.canonical_code = 0;
    entry.read_order_code = 0;
    entry.canonical_bits = "0";
    entry.read_order_bits = "0";
    entry.provenance_range = {pnga::trace_model::DeflateBitOffset{0},
                              pnga::trace_model::DeflateBitOffset{1}};
    table.entries.push_back(entry);
  }
  huffman.tables.push_back(table);
  pnga::ui::qt::HuffmanInspector huffman_widget;
  huffman_widget.setView(huffman);
  // WP-5U12D mechanism migration: the Huffman page is model-backed
  // (QTableView compressionHuffmanTable, no QTableWidget), so the former
  // verify_bounded_table call (capped QTableWidget row count plus a
  // "truncated" marker row) is asserted as the virtualized model row count
  // equal to the same input volume with a real last row, mirroring the
  // Blocks ruling above. The Decode Trace call site below is untouched.
  auto* huffman_table = huffman_widget.findChild<QTableView*>(
      QStringLiteral("compressionHuffmanTable"));
  QVERIFY(huffman_table != nullptr);
  QVERIFY(huffman_table->model() != nullptr);
  QCOMPARE(huffman_table->model()->rowCount(),
           pnga::ui::qt::HuffmanInspector::kMaxVisibleRows + 1);
  QCOMPARE(huffman_table->model()
               ->data(huffman_table->model()->index(
                          pnga::ui::qt::HuffmanInspector::kMaxVisibleRows, 0),
                      Qt::DisplayRole)
               .toString(),
           QString::number(static_cast<qulonglong>(
               pnga::ui::qt::HuffmanInspector::kMaxVisibleRows)));

  pnga::analysis_engine::DecodeTraceInspectorView decode;
  for (int i = 0; i < pnga::ui::qt::DecodeTraceInspector::kMaxVisibleRows + 1;
       ++i) {
    pnga::analysis_engine::DecodeTraceStep step;
    step.token_index = static_cast<std::uint64_t>(i);
    decode.steps.push_back(step);
  }
  pnga::ui::qt::DecodeTraceInspector decode_widget;
  decode_widget.setView(decode);
  // WP-5U12E mechanism migration: the Decode Trace page is model-backed
  // (QTableView compressionDecodeTraceTable, no QTableWidget), so the former
  // verify_bounded_table call (capped QTableWidget row count plus a
  // "truncated" marker row) is asserted as the virtualized model row count
  // equal to the same input volume with a real last row, mirroring the
  // Blocks and Huffman rulings above. The input volume is unchanged.
  auto* decode_table = decode_widget.findChild<QTableView*>(
      QStringLiteral("compressionDecodeTraceTable"));
  QVERIFY(decode_table != nullptr);
  QVERIFY(decode_table->model() != nullptr);
  QCOMPARE(decode_table->model()->rowCount(),
           pnga::ui::qt::DecodeTraceInspector::kMaxVisibleRows + 1);
  QCOMPARE(decode_table->model()
                ->data(decode_table->model()->index(
                          pnga::ui::qt::DecodeTraceInspector::kMaxVisibleRows,
                          1),
                       Qt::DisplayRole)
                .toString(),
            QString::number(static_cast<qulonglong>(
                pnga::ui::qt::DecodeTraceInspector::kMaxVisibleRows)));
}

QTEST_MAIN(CrossPlatformGuiGateTest)
#include "cross_platform_gui_gate_test.moc"
