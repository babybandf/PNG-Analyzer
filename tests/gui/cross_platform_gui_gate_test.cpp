// WP-5U6C: deterministic GUI gate for layout, DPI/theme resilience,
// shortcuts, focus order and basic accessibility metadata. The test never
// opens a file or invokes a decoder.

#include "main_window.h"

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/huffman_inspector.h>

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
#include <QScreen>
#include <QSpinBox>
#include <QSplitter>
#include <QSysInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QtGlobal>

#include <cstddef>
#include <cstdint>

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
  void shortcutsAndFocusOrderAreStable();
  void accessibilityNamesCoverControlsAndInspectors();
  void inspectorTruncationContractsRemainBounded();
};

void CrossPlatformGuiGateTest::initTestCase() {
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
  QCOMPARE(inspector->count(), 8);

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
  auto* base = window.findChild<QComboBox*>(QStringLiteral("numericBase"));
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

void CrossPlatformGuiGateTest::shortcutsAndFocusOrderAreStable() {
  MainWindow window;
  auto* x = window.findChild<QSpinBox*>(QStringLiteral("xCoordinate"));
  auto* y = window.findChild<QSpinBox*>(QStringLiteral("yCoordinate"));
  auto* lock = window.findChild<QCheckBox*>(QStringLiteral("lockCoordinate"));
  auto* base = window.findChild<QComboBox*>(QStringLiteral("numericBase"));
  auto* source = window.findChild<QComboBox*>(QStringLiteral("hexSource"));
  auto* follow = window.findChild<QCheckBox*>(QStringLiteral("hexFollowPixel"));
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(x != nullptr);
  QVERIFY(y != nullptr);
  QVERIFY(lock != nullptr);
  QVERIFY(base != nullptr);
  QVERIFY(source != nullptr);
  QVERIFY(follow != nullptr);
  QVERIFY(tabs != nullptr);
  QVERIFY(focus_chain_reaches(x, y));
  QVERIFY(focus_chain_reaches(y, lock));
  QVERIFY(focus_chain_reaches(lock, base));
  QVERIFY(focus_chain_reaches(base, source));
  QVERIFY(focus_chain_reaches(source, follow));
  QVERIFY(focus_chain_reaches(follow, tabs));

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
  check("hexSource");
  check("hexFollowPixel");
  check("inspectorTabs");
  check("reconstructInspector");
  check("pixelInspector");
  check("scanlineInspector");
  check("sourceInspector");
  check("formatContextInspector");
  check("blockInspector");
  check("huffmanInspector");
  check("decodeTraceInspector");
  check("validationStatus");
  check("previewTabs");
  check("coordinateToolbar");
  check("hexView");
  auto* inspector = window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(inspector != nullptr);
  for (int i = 0; i < inspector->count(); ++i) {
    QVERIFY(!inspector->tabText(i).trimmed().isEmpty());
    QVERIFY(!inspector->widget(i)->accessibleName().trimmed().isEmpty());
  }
}

void CrossPlatformGuiGateTest::inspectorTruncationContractsRemainBounded() {
  pnga::analysis_engine::BlockInspectorView block;
  for (int i = 0; i < pnga::ui::qt::BlockInspector::kMaxVisibleRows + 1; ++i) {
    pnga::analysis_engine::BlockInspectorRow row;
    row.block_index = static_cast<std::uint64_t>(i);
    block.rows.push_back(row);
  }
  pnga::ui::qt::BlockInspector block_widget;
  block_widget.setView(block);
  verify_bounded_table(block_widget,
                       pnga::ui::qt::BlockInspector::kMaxVisibleRows + 1);

  pnga::analysis_engine::HuffmanInspectorView huffman;
  pnga::analysis_engine::HuffmanInspectorTable table;
  for (int i = 0; i < pnga::ui::qt::HuffmanInspector::kMaxVisibleRows + 1;
       ++i) {
    table.entries.push_back({static_cast<std::uint16_t>(i), 1, 0, 0, 1,
                             false});
  }
  huffman.tables.push_back(table);
  pnga::ui::qt::HuffmanInspector huffman_widget;
  huffman_widget.setView(huffman);
  verify_bounded_table(
      huffman_widget, pnga::ui::qt::HuffmanInspector::kMaxVisibleRows + 1);

  pnga::analysis_engine::DecodeTraceInspectorView decode;
  for (int i = 0; i < pnga::ui::qt::DecodeTraceInspector::kMaxVisibleRows + 1;
       ++i) {
    pnga::analysis_engine::DecodeTraceStep step;
    step.token_index = static_cast<std::uint64_t>(i);
    decode.steps.push_back(step);
  }
  pnga::ui::qt::DecodeTraceInspector decode_widget;
  decode_widget.setView(decode);
  verify_bounded_table(
      decode_widget, pnga::ui::qt::DecodeTraceInspector::kMaxVisibleRows + 1);
}

QTEST_MAIN(CrossPlatformGuiGateTest)
#include "cross_platform_gui_gate_test.moc"
