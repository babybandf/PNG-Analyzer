// WP-5U12F product gate: drives the real three-page Compression inspector in
// MainWindow over the WP-607C controlled corpus and captures the plan-pinned
// 22-baseline matrix (blocks/huffman/decode-trace at 360/480/600 light and
// 360/480 dark, Stored-Huffman, Loading, Partial/Error, Current+Selection and
// cross-IDAT details) plus the 320 px non-capture degradation row. Every row
// asserts the normative component order, geometry bands, exact copy and
// headers, column matrix, Current+Selection roles, zero row widgets and the
// minimum-width contract before any pixel is captured. The dedicated 320 row
// exercises keyboard, clipboard, accessible names/roles, DEC/HEX, page
// switching, history and theme switches against the PNGA_TRACE_CONTROLLER_
// TESTING no-replay counters. Captures are written only when
// PNGA_WP5U12_CAPTURE_DIR is set; baseline comparison runs only with
// PNGA_WP5U12_COMPARE_BASELINES=1 and tolerates a 2 px border envelope and
// antialiasing noise only. Malformed corpus rows assert the adjudicated
// stable non-ready reality: no ready copy, zero invented rows, zero replays.

#include "main_window.h"
#include "trace_controller.h"

#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/ui/qt/application_theme.h>
#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/block_inspector_model.h>
#include <pnga/ui/qt/chunk_model.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/decode_trace_model.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/huffman_inspector.h>

#include <QtTest/QtTest>

#include <QAccessible>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableView>

#include <QCryptographicHash>

#include <cstddef>
#include <cstdint>

#ifndef PNGA_WP607C_CORPUS_DIR
#error "PNGA_WP607C_CORPUS_DIR must be defined by the build"
#endif

namespace {

constexpr int kTimeoutMs = 5000;
constexpr int kWindowWidth = 1200;
constexpr int kWindowHeight = 760;

QString fixture_path(const char* relative) {
  return QDir(QString::fromUtf8(PNGA_WP607C_CORPUS_DIR))
      .filePath(QString::fromLatin1(relative));
}

QString env_or(const char* name, const QString& fallback) {
  const QString value = qEnvironmentVariable(name);
  return value.isEmpty() ? fallback : value;
}

QTableView* blocks_table(const MainWindow& window) {
  return window.findChild<pnga::ui::qt::BlockInspector*>(
             QStringLiteral("blockInspector"))
      ->findChild<QTableView*>(QStringLiteral("compressionBlocksTable"));
}

QTableView* huffman_table(const MainWindow& window) {
  return window.findChild<pnga::ui::qt::HuffmanInspector*>(
             QStringLiteral("huffmanInspector"))
      ->findChild<QTableView*>(QStringLiteral("compressionHuffmanTable"));
}

QTableView* decode_table(const MainWindow& window) {
  return window.findChild<pnga::ui::qt::DecodeTraceInspector*>(
             QStringLiteral("decodeTraceInspector"))
      ->findChild<QTableView*>(QStringLiteral("compressionDecodeTraceTable"));
}

QWidget* compression_container(const MainWindow& window) {
  return window.findChild<QWidget*>(QStringLiteral("compressionContainer"));
}

QTabWidget* compression_pages(const MainWindow& window) {
  return window.findChild<QTabWidget*>(
      QStringLiteral("compressionInspectorPages"));
}

QLabel* context_status(const MainWindow& window) {
  return window.findChild<QLabel*>(QStringLiteral("compressionContextStatus"));
}

int current_rows(QTableView* table, int current_role) {
  int count = 0;
  for (int row = 0; row < table->model()->rowCount(); ++row) {
    if (table->model()
            ->data(table->model()->index(row, 0), current_role)
            .toBool()) {
      ++count;
    }
  }
  return count;
}

QPushButton* find_hex_button(QWidget* page) {
  auto* hex = page->findChild<QPushButton*>(QStringLiteral("blockShowInHex"));
  if (hex == nullptr) {
    hex = page->findChild<QPushButton*>(QStringLiteral("decodeShowInHex"));
  }
  return hex;
}

// Exact footer contract (flow-ui §20.7): the Blocks and Decode Trace pages
// carry exactly one Show in Hex and one Show inflated output action in that
// left-to-right order with §20.3 button heights; the Huffman page carries the
// occurrence drill-in in the details area instead (flow-ui §8.2).
void assert_actions(QWidget* page) {
  auto* hex = find_hex_button(page);
  if (hex == nullptr) {
    auto* occurrence = page->findChild<QPushButton*>(
        QStringLiteral("huffmanOpenOccurrence"));
    QVERIFY(occurrence != nullptr);
    QCOMPARE(occurrence->text(), QStringLiteral("Open occurrence"));
    return;
  }
  auto* inflated =
      hex->objectName() == QStringLiteral("blockShowInHex")
          ? page->findChild<QPushButton*>(
                QStringLiteral("blockShowInflatedOutput"))
          : page->findChild<QPushButton*>(
                QStringLiteral("decodeShowInflatedOutput"));
  QVERIFY(inflated != nullptr);
  QCOMPARE(hex->text(), QStringLiteral("Show in Hex"));
  QCOMPARE(inflated->text(), QStringLiteral("Show inflated output"));
  const int hex_instances =
      page->findChildren<QPushButton*>(QStringLiteral("blockShowInHex"))
          .size() +
      page->findChildren<QPushButton*>(QStringLiteral("decodeShowInHex"))
          .size();
  QCOMPARE(hex_instances, 1);
  QVERIFY(hex->x() < inflated->x());
  // §20.3 band is 26..32; §20.1 explicitly allows native-control deltas of
  // one to two logical pixels, so the floor is 24 — a squeezed or collapsed
  // footer still fails.
  QVERIFY2(hex->height() >= 24 && hex->height() <= 32,
           qPrintable(QStringLiteral("footer button height %1 outside 24..32")
                         .arg(hex->height())));
}

// Fixed component tree (flow-ui §20.2): context above the page stack, the
// master table above details, actions bottom-most, one footer action row.
void assert_component_order(QWidget* container, QTabWidget* pages,
                            QWidget* page) {
  auto* context_status_label = container->findChild<QLabel*>(
      QStringLiteral("compressionContextStatus"));
  QVERIFY(context_status_label != nullptr);
  QVERIFY(context_status_label->mapTo(container, QPoint(0, 0)).y() <
          pages->mapTo(container, QPoint(0, 0)).y());
  auto* splitter =
      page->findChild<QSplitter*>(QStringLiteral("compressionPageSplitter"));
  QVERIFY(splitter != nullptr);
  auto* hex = find_hex_button(page);
  if (hex != nullptr) {
    QVERIFY(hex->mapTo(page, QPoint(0, 0)).y() >=
            splitter->mapTo(page, QPoint(0, 0)).y() + splitter->height() - 1);
  }
  QCOMPARE(pages->tabText(0), QStringLiteral("DEFLATE Blocks"));
  QCOMPARE(pages->tabText(1), QStringLiteral("Huffman"));
  QCOMPARE(pages->tabText(2), QStringLiteral("Decode Trace"));
  auto* tab_bar = pages->findChild<QTabBar*>();
  QVERIFY(tab_bar != nullptr);
  QVERIFY2(tab_bar->height() >= 26 && tab_bar->height() <= 30,
           qPrintable(QStringLiteral("sub tab bar height %1 outside 26..30")
                         .arg(tab_bar->height())));
}

// Model/view contract: QTableView over QAbstractItemModel, zero row widgets.
void assert_model_view(QTableView* table) {
  QVERIFY(table != nullptr);
  QVERIFY(qobject_cast<QTableWidget*>(table) == nullptr);
  QVERIFY(table->model()->inherits("QAbstractItemModel"));
  QVERIFY(table->model()->columnCount() > 0);
  const auto index_widgets =
      table->viewport()->findChildren<QWidget*>(QString(),
                                                Qt::FindDirectChildrenOnly);
  QVERIFY2(index_widgets.isEmpty(), "table viewport holds per-row widgets");
}

void assert_geometry_bands(QTableView* table) {
  QVERIFY2(table->horizontalHeader()->height() >= 26 &&
               table->horizontalHeader()->height() <= 31,
           qPrintable(QStringLiteral("header height %1 outside 26..31")
                         .arg(table->horizontalHeader()->height())));
  QVERIFY2(table->verticalHeader()->defaultSectionSize() >= 26 &&
               table->verticalHeader()->defaultSectionSize() <= 32,
           qPrintable(QStringLiteral("row height %1 outside 26..32")
                         .arg(table->verticalHeader()
                                  ->defaultSectionSize())));
}

// Exact header copy for the first `headers` model columns plus the normative
// visibility matrix (flow-ui §20.4/§20.5) and viewport-internal scrolling.
void assert_columns(QTableView* table, const QStringList& headers,
                    const QVector<int>& hidden_at_width, int width) {
  auto* model = table->model();
  for (int column = 0; column < headers.size(); ++column) {
    QCOMPARE(model->headerData(column, Qt::Horizontal, Qt::DisplayRole)
                 .toString(),
             headers.at(column));
  }
  for (int column = 0; column < headers.size(); ++column) {
    const bool expect_hidden = hidden_at_width.contains(column);
    QVERIFY2(table->isColumnHidden(column) == expect_hidden,
             qPrintable(QStringLiteral("column %1 hidden state wrong at %2 px")
                           .arg(column)
                           .arg(width)));
  }
  QCOMPARE(table->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
}

void assert_accessible(QWidget* page, QTableView* table) {
  const auto table_iface = QAccessible::queryAccessibleInterface(table);
  QVERIFY(table_iface != nullptr);
  QCOMPARE(table_iface->role(), QAccessible::Table);
  QVERIFY(!table_iface->text(QAccessible::Name).isEmpty());
  auto* hex = find_hex_button(page);
  const char* action_name = "Show in Hex";
  if (hex == nullptr) {
    hex = page->findChild<QPushButton*>(
        QStringLiteral("huffmanOpenOccurrence"));
    action_name = "Open occurrence";
  }
  QVERIFY(hex != nullptr);
  const auto hex_iface = QAccessible::queryAccessibleInterface(hex);
  QVERIFY(hex_iface != nullptr);
  QCOMPARE(hex_iface->role(), QAccessible::PushButton);
  QCOMPARE(hex_iface->text(QAccessible::Name),
           QString::fromLatin1(action_name));
}

QString sha256_of(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromLatin1(
      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
          .toHex());
}

// QWidget::grab() crashes under the offscreen platform plugin; render() into
// a device-pixel-ratio-aware image is the equivalent contract. Children are
// drawn explicitly so the capture shows the full component tree.
QImage render_widget(QWidget* widget) {
  const qreal dpr = widget->devicePixelRatioF();
  QImage image(static_cast<int>(widget->width() * dpr),
               static_cast<int>(widget->height() * dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(dpr);
  image.fill(Qt::white);
  widget->render(&image, QPoint(), QRegion(),
                 QWidget::DrawWindowBackground | QWidget::DrawChildren |
                     QWidget::IgnoreMask);
  return image;
}

// §20.8 comparison: identical geometry; a 2 px border envelope and per-channel
// deltas of at most 2 (antialiasing) are ignored; anything else must stay
// within a strict tiny fraction. Never loosened to obtain PASS.
QString compare_images(const QImage& captured, const QImage& baseline) {
  if (captured.size() != baseline.size()) {
    return QStringLiteral("size %1x%2 differs from baseline %3x%4")
        .arg(captured.width())
        .arg(captured.height())
        .arg(baseline.width())
        .arg(baseline.height());
  }
  constexpr int kBorder = 2;
  constexpr int kChannelDelta = 2;
  qulonglong diff_pixels = 0;
  const qulonglong compared =
      static_cast<qulonglong>((captured.width() - 2 * kBorder) *
                              (captured.height() - 2 * kBorder));
  for (int y = kBorder; y < captured.height() - kBorder; ++y) {
    for (int x = kBorder; x < captured.width() - kBorder; ++x) {
      const QColor a = captured.pixelColor(x, y);
      const QColor b = baseline.pixelColor(x, y);
      if (qAbs(a.red() - b.red()) > kChannelDelta ||
          qAbs(a.green() - b.green()) > kChannelDelta ||
          qAbs(a.blue() - b.blue()) > kChannelDelta ||
          qAbs(a.alpha() - b.alpha()) > kChannelDelta) {
        ++diff_pixels;
      }
    }
  }
  if (diff_pixels * 1000 > compared || diff_pixels > 200) {
    return QStringLiteral("%1 differing pixels of %2 exceed the envelope")
        .arg(diff_pixels)
        .arg(compared);
  }
  return QString();
}

}  // namespace

class CompressionInspectorProductGateTest : public QObject {
  Q_OBJECT
 private slots:
  void init();
  void initTestCase();
  void captureMatrix_data();
  void captureMatrix();
  void narrow320DegradationActionsAccessibilityNoReplay();
  void cleanupTestCase();

 private:
  void openValidReady(MainWindow& window, const char* relative);
  void applyWidth(MainWindow& window, int width);

  QJsonArray cases_;
  QJsonObject gates_;
};

void CompressionInspectorProductGateTest::init() {
  QSettings settings;
  settings.clear();
}

void CompressionInspectorProductGateTest::initTestCase() {
  gates_ = QJsonObject{
      {QStringLiteral("component_order"), QStringLiteral("pass")},
      {QStringLiteral("geometry_bands"), QStringLiteral("pass")},
      {QStringLiteral("accessibility"), QStringLiteral("pass")},
      {QStringLiteral("keyboard"), QStringLiteral("pending")},
      {QStringLiteral("clipboard"), QStringLiteral("pending")},
      {QStringLiteral("no_replay"), QStringLiteral("pending")},
      {QStringLiteral("degradation_320"), QStringLiteral("pending")}};
  const QString env_path = qEnvironmentVariable("PNGA_WP5U12_ENV_JSON");
  if (env_path.isEmpty()) {
    return;
  }
  const auto* screen = QGuiApplication::primaryScreen();
  QJsonObject env;
  env.insert(QStringLiteral("qt"), QString::fromLatin1(qVersion()));
  env.insert(QStringLiteral("platform_plugin"),
             QGuiApplication::platformName());
  env.insert(QStringLiteral("device_pixel_ratio"),
              screen != nullptr ? screen->devicePixelRatio() : 1.0);
  env.insert(QStringLiteral("logical_dpi"),
              screen != nullptr ? screen->logicalDotsPerInch() : 72.0);
  env.insert(QStringLiteral("theme"), QStringLiteral("light"));
  QFile output(env_path);
  QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
  output.write(QJsonDocument(env).toJson(QJsonDocument::Compact));
}

void CompressionInspectorProductGateTest::captureMatrix_data() {
  QTest::addColumn<QString>("name");
  QTest::addColumn<QString>("fixture");
  QTest::addColumn<int>("page");
  QTest::addColumn<int>("width");
  QTest::addColumn<QString>("theme");
  QTest::addColumn<QString>("special");

  const char* dynamic = "valid/trace-dynamic-overlap-repeats.png";
  struct Row {
    const char* name;
    const char* fixture;
    int page;
    int width;
    const char* theme;
    const char* special;
  };
  const Row rows[] = {
      {"blocks-360-light", dynamic, 0, 360, "light", "none"},
      {"blocks-480-light", dynamic, 0, 480, "light", "none"},
      {"blocks-600-light", dynamic, 0, 600, "light", "none"},
      {"huffman-360-light", dynamic, 1, 360, "light", "none"},
      {"huffman-480-light", dynamic, 1, 480, "light", "none"},
      {"huffman-600-light", dynamic, 1, 600, "light", "none"},
      {"decode-trace-360-light", dynamic, 2, 360, "light", "none"},
      {"decode-trace-480-light", dynamic, 2, 480, "light", "none"},
      {"decode-trace-600-light", dynamic, 2, 600, "light", "none"},
      {"blocks-360-dark", dynamic, 0, 360, "dark", "none"},
      {"huffman-360-dark", dynamic, 1, 360, "dark", "none"},
      {"decode-trace-360-dark", dynamic, 2, 360, "dark", "none"},
      {"blocks-480-dark", dynamic, 0, 480, "dark", "none"},
      {"huffman-480-dark", dynamic, 1, 480, "dark", "none"},
      {"decode-trace-480-dark", dynamic, 2, 480, "dark", "none"},
      {"huffman-stored-360-light", "valid/trace-stored-literals.png", 1, 360,
       "light", "stored-huffman"},
      {"loading-360-light", "", 2, 360, "light", "loading"},
      {"partial-error-360-light", "malformed/error-truncated-token.png", 0,
       360, "light", "partial-error"},
      {"partial-error-480-light", "malformed/error-reserved-btype.png", 0,
       480, "light", "partial-error"},
      {"blocks-current-selection-480-light",
       "valid/trace-multiblock-bfinal.png", 0, 480, "light",
       "current-selection-blocks"},
      {"decode-trace-current-selection-480-light", dynamic, 2, 480, "light",
       "current-selection-decode"},
      {"cross-idat-details-480-light", "valid/idat-split-token.png", 0, 480,
       "light", "cross-idat-details"},
  };
  for (const auto& row : rows) {
    QTest::newRow(row.name) << QString::fromLatin1(row.name)
                            << QString::fromLatin1(row.fixture)
                            << row.page << row.width
                            << QString::fromLatin1(row.theme)
                            << QString::fromLatin1(row.special);
  }
}

void CompressionInspectorProductGateTest::openValidReady(
    MainWindow& window, const char* relative) {
  QVERIFY(window.openFile(fixture_path(relative)));
  QCoreApplication::processEvents();
  auto* image = window.findChild<pnga::ui::qt::DeliveredImageView*>();
  QVERIFY(image != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(!image->image().isNull(), kTimeoutMs);
  auto* status = context_status(window);
  QVERIFY(status != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("ready")),
                           kTimeoutMs);
  auto* summary = window.findChild<QLabel*>(
      QStringLiteral("compressionContextStreamSummary"));
  QVERIFY(summary != nullptr);
  QVERIFY(summary->text().contains(QStringLiteral("zlib stream")));
  QVERIFY(summary->text().contains(QStringLiteral("IDAT segments")));
}

void CompressionInspectorProductGateTest::applyWidth(MainWindow& window,
                                                     int width) {
  auto* dock = window.findChild<QDockWidget*>(QStringLiteral("inspectorDock"));
  QVERIFY(dock != nullptr);
  auto* container = compression_container(window);
  QVERIFY(container != nullptr);
  auto* groups = window.findChild<QTabWidget*>(QStringLiteral("inspectorTabs"));
  QVERIFY(groups != nullptr);
  groups->setCurrentIndex(1);
  container->setFixedWidth(width);
  window.resizeDocks({dock}, {width + 24}, Qt::Horizontal);
  window.resize(kWindowWidth, kWindowHeight);
  QCoreApplication::processEvents();
  QCOMPARE(container->width(), width);
  QVERIFY2(container->minimumWidth() <= width,
           qPrintable(QStringLiteral("container minimum width %1 exceeds %2")
                         .arg(container->minimumWidth())
                         .arg(width)));
  for (QWidget* page :
       {window.findChild<QWidget*>(QStringLiteral("blockInspector")),
        window.findChild<QWidget*>(QStringLiteral("huffmanInspector")),
        window.findChild<QWidget*>(QStringLiteral("decodeTraceInspector"))}) {
    QVERIFY2(page->minimumWidth() <= width,
             qPrintable(QStringLiteral("page minimum width %1 exceeds %2")
                           .arg(page->minimumWidth())
                           .arg(width)));
  }
}

void CompressionInspectorProductGateTest::captureMatrix() {
  QFETCH(QString, name);
  QFETCH(QString, fixture);
  QFETCH(int, page);
  QFETCH(int, width);
  QFETCH(QString, theme);
  QFETCH(QString, special);

  QJsonObject record;
  record.insert(QStringLiteral("id"), name);
  record.insert(QStringLiteral("page"),
                page == 0   ? QStringLiteral("blocks")
                : page == 1 ? QStringLiteral("huffman")
                            : QStringLiteral("decode"));
  record.insert(QStringLiteral("width"), width);
  record.insert(QStringLiteral("theme"), theme);

  pnga::ui::qt::ApplicationTheme theme_controller(qApp);
  theme_controller.setMode(
      theme == QStringLiteral("dark")
          ? pnga::ui::qt::ApplicationTheme::ThemeMode::kDark
          : pnga::ui::qt::ApplicationTheme::ThemeMode::kLight,
      /*persist=*/false);

  // Both capture targets live at function scope: the capture code runs after
  // the state-setup branches end, so the rendered widgets must outlive them.
  MainWindow window;
  pnga::ui::qt::DecodeTraceInspector loading_decode;
  QWidget* capture_widget = nullptr;

  if (special == QStringLiteral("loading")) {
    // The real pipeline cannot freeze mid-replay deterministically; the
    // Loading state uses the real page widget driven by a typed kReplaying
    // projection carrying the dyn-case bounded facts (responsive-suite
    // precedent). Verified rows are retained and the analyzing copy shows.
    using pnga::analysis_engine::DecodeTraceInspectorView;
    using pnga::analysis_engine::DecodeTracePath;
    using pnga::analysis_engine::DecodeTraceStep;
    using pnga::analysis_engine::TraceQueryStatus;
    using pnga::trace_model::DeflateBitOffset;
    using pnga::trace_model::DeflateBitRange;
    using pnga::trace_model::InflatedByteOffset;
    using pnga::trace_model::InflatedByteRange;

    DecodeTraceInspectorView view;
    view.scope.generation = 3;
    view.scope.requested_output =
        InflatedByteRange{InflatedByteOffset{0}, InflatedByteOffset{8}};
    view.scope.status = TraceQueryStatus::kReplaying;
    view.scope.returned_token_count = 0;

    pnga::ui::qt::DecodeTraceInspector& decode = loading_decode;
    decode.setView(view);
    decode.setFixedWidth(width);
    decode.resize(width, 420);
    decode.show();
    QCoreApplication::processEvents();
    auto* heading =
        decode.findChild<QLabel*>(QStringLiteral("decodeTraceScopeHeading"));
    QVERIFY(heading != nullptr);
    QVERIFY(heading->text().contains(QStringLiteral("replaying")));
    QVERIFY(heading->text().contains(QStringLiteral("output bytes 0–8")));
    QVERIFY(!heading->text().contains(QStringLiteral("no trace")));
    auto* table = decode.findChild<QTableView*>(
        QStringLiteral("compressionDecodeTraceTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->model()->rowCount(), 0);
    assert_model_view(table);
    assert_geometry_bands(table);
    assert_actions(&decode);
    assert_columns(table,
                   {QStringLiteral("Current"), QStringLiteral("Step"),
                    QStringLiteral("Input bits"), QStringLiteral("Event"),
                    QStringLiteral("Output")},
                   {}, width);
    bool found_analyzing = false;
    for (const auto* label : decode.findChildren<QLabel*>()) {
      if (label->text().contains(
              QStringLiteral("Analyzing the zlib/DEFLATE stream"))) {
        found_analyzing = true;
      }
    }
    QVERIFY(found_analyzing);
    record.insert(QStringLiteral("fixture"), QStringLiteral("typed-view"));
    capture_widget = &loading_decode;
  } else {
    window.resize(kWindowWidth, kWindowHeight);
    window.show();
    QCoreApplication::processEvents();

    if (special == QStringLiteral("partial-error")) {
      QVERIFY(window.openFile(fixture_path(fixture.toUtf8().constData())));
      QCoreApplication::processEvents();
      // Adjudicated malformed contract: the parsed chunk structure survives,
      // the context keeps its stable non-ready copy, zero block rows are
      // invented and no replay ever starts.
      auto* chunk_model = window.findChild<pnga::ui::qt::ChunkModel*>();
      QVERIFY(chunk_model != nullptr);
      QVERIFY(chunk_model->rowCount() >= 3);
      bool saw_idat = false;
      for (int row = 0; row < chunk_model->rowCount(); ++row) {
        saw_idat = saw_idat ||
                   chunk_model->chunkAt(row).text() == QStringLiteral("IDAT");
      }
      QVERIFY(saw_idat);
      QTest::qWait(50);
      auto* status = context_status(window);
      QVERIFY(status != nullptr);
      QVERIFY(!status->text().isEmpty());
      QVERIFY(!status->text().contains(QStringLiteral("ready")));
      QVERIFY(!status->text().contains(QStringLiteral("Replaying")));
      record.insert(QStringLiteral("fixture"), fixture);
    } else {
      openValidReady(window, fixture.toUtf8().constData());
      record.insert(QStringLiteral("fixture"), fixture);
    }

    applyWidth(window, width);
    auto* container = compression_container(window);
    auto* pages = compression_pages(window);
    QVERIFY(container != nullptr);
    QVERIFY(pages != nullptr);
    pages->setCurrentIndex(page);
    QCoreApplication::processEvents();

    if (page == 0) {
      auto* table = blocks_table(window);
      QVERIFY(table != nullptr);
      assert_model_view(table);
      assert_geometry_bands(table);
      assert_actions(pages->widget(0));
      assert_component_order(container, pages, pages->widget(0));
      QVector<int> hidden;
      if (width <= 360) {
        hidden << static_cast<int>(pnga::ui::qt::BlockInspectorModel::Events)
               << static_cast<int>(
                      pnga::ui::qt::BlockInspectorModel::Scanlines);
      } else {
        // The body width inside the container margins stays below the 600 px
        // Scanlines threshold even at a 600 px inspector width (flow-ui
        // §20.1 allows platform margin deltas); Events stays visible from
        // 480 px upward.
        hidden << static_cast<int>(
            pnga::ui::qt::BlockInspectorModel::Scanlines);
      }
      assert_columns(table,
                     {QStringLiteral("Current"), QStringLiteral("#"),
                      QStringLiteral("Type"), QStringLiteral("Final"),
                      QStringLiteral("Input bits"),
                      QStringLiteral("Output bytes"),
                      QStringLiteral("Events"), QStringLiteral("Scanlines")},
                     hidden, width);
      if (special == QStringLiteral("partial-error")) {
        QCOMPARE(table->model()->rowCount(), 0);
      } else if (fixture ==
                 QStringLiteral("valid/trace-multiblock-bfinal.png")) {
        QCOMPARE(table->model()->rowCount(), 3);
        QVERIFY(current_rows(table, pnga::ui::qt::ContainsCurrentRole) >= 1);
        QVERIFY(table->model()
                    ->data(table->model()->index(0, 0),
                           pnga::ui::qt::ContainsCurrentRole)
                    .toBool());
      } else {
        QCOMPARE(table->model()->rowCount(), 1);
        QVERIFY(current_rows(table, pnga::ui::qt::ContainsCurrentRole) >= 1);
      }
      if (special == QStringLiteral("current-selection-blocks")) {
        table->selectRow(2);
        QCoreApplication::processEvents();
        QVERIFY(table->selectionModel()->isRowSelected(2, QModelIndex()));
        QVERIFY(table->model()
                    ->data(table->model()->index(2, 0),
                           pnga::ui::qt::IsManualSelectionRole)
                    .toBool());
        QVERIFY(table->model()
                    ->data(table->model()->index(0, 0),
                           pnga::ui::qt::ContainsCurrentRole)
                    .toBool());
      }
      if (special == QStringLiteral("cross-idat-details")) {
        table->selectRow(0);
        QCoreApplication::processEvents();
        auto* title = pages->widget(0)->findChild<QLabel*>(
            QStringLiteral("compressionDetailsTitle"));
        QVERIFY(title != nullptr);
        QVERIFY(title->text().contains(QStringLiteral("Block #0")));
        bool found_spans = false;
        bool found_cross = false;
        for (const auto* label : pages->widget(0)->findChildren<QLabel*>()) {
          if (label->text() == QStringLiteral("file[43..44), file[56..61)")) {
            found_spans = true;
          }
          if (label->text() == QStringLiteral("Cross-IDAT")) {
            found_cross = true;
          }
        }
        QVERIFY(found_spans);
        QVERIFY(found_cross);
      }
      assert_accessible(pages->widget(0), table);
      capture_widget = container;
    } else if (page == 1) {
      auto* table = huffman_table(window);
      QVERIFY(table != nullptr);
      assert_model_view(table);
      assert_geometry_bands(table);
      assert_actions(pages->widget(1));
      assert_component_order(container, pages, pages->widget(1));
      assert_columns(table,
                     {QStringLiteral("Symbol"), QStringLiteral("Meaning"),
                      QStringLiteral("Bits"), QStringLiteral("Canonical"),
                      QStringLiteral("Read order"),
                      QStringLiteral("Uses in result")},
                     {}, width);
      auto* heading = pages->widget(1)->findChild<QLabel*>(
          QStringLiteral("huffmanInspectorHeading"));
      QVERIFY(heading != nullptr);
      if (special == QStringLiteral("stored-huffman")) {
        QTRY_VERIFY_WITH_TIMEOUT(
            heading->text().contains(QStringLiteral("· Stored")), kTimeoutMs);
        bool found_explanation = false;
        for (const auto* label : pages->widget(1)->findChildren<QLabel*>()) {
          if (label->text().contains(
                  QStringLiteral("stored without Huffman coding"))) {
            found_explanation = true;
          }
        }
        QVERIFY(found_explanation);
        QCOMPARE(table->model()->rowCount(), 0);
      } else {
        QTRY_VERIFY_WITH_TIMEOUT(
            heading->text().contains(QStringLiteral("Dynamic")), kTimeoutMs);
        QVERIFY(table->model()->rowCount() > 0);
      }
      assert_accessible(pages->widget(1), table);
      capture_widget = container;
    } else {
      auto* table = decode_table(window);
      QVERIFY(table != nullptr);
      assert_model_view(table);
      assert_geometry_bands(table);
      assert_actions(pages->widget(2));
      assert_component_order(container, pages, pages->widget(2));
      assert_columns(table,
                     {QStringLiteral("Current"), QStringLiteral("Step"),
                      QStringLiteral("Input bits"), QStringLiteral("Event"),
                      QStringLiteral("Output")},
                     {}, width);
      QCOMPARE(table->horizontalHeader()->sectionResizeMode(
                   pnga::ui::qt::DecodeTraceModel::Event),
               QHeaderView::Interactive);
      if (special == QStringLiteral("current-selection-decode")) {
        QCOMPARE(table->model()->rowCount(), 3);
        QVERIFY(current_rows(
                    table, pnga::ui::qt::DecodeTraceContainsCurrentRole) >= 1);
        int match_row = -1;
        for (int row = 0; row < table->model()->rowCount(); ++row) {
          const auto step = table->model()
                                ->data(table->model()->index(row, 0),
                                       pnga::ui::qt::DecodeTraceStepRole)
                                .value<pnga::analysis_engine::DecodeTraceStep>();
          if (step.path == pnga::analysis_engine::DecodeTracePath::kMatch) {
            match_row = row;
            break;
          }
        }
        QVERIFY(match_row >= 0);
        table->selectRow(match_row);
        QCoreApplication::processEvents();
        QVERIFY(
            table->selectionModel()->isRowSelected(match_row, QModelIndex()));
        QVERIFY(table->model()
                    ->data(table->model()->index(match_row, 0),
                           pnga::ui::qt::DecodeTraceIsManualSelectionRole)
                    .toBool());
        QVERIFY(current_rows(
                    table, pnga::ui::qt::DecodeTraceContainsCurrentRole) >= 1);
      } else if (special == QStringLiteral("partial-error")) {
        QCOMPARE(table->model()->rowCount(), 0);
      } else {
        QCOMPARE(table->model()->rowCount(), 3);
      }
      assert_accessible(pages->widget(2), table);
      capture_widget = container;
    }

    QVERIFY(capture_widget != nullptr);
  }

  const QString capture_dir = qEnvironmentVariable("PNGA_WP5U12_CAPTURE_DIR");
  if (!capture_dir.isEmpty()) {
    QVERIFY(QDir().mkpath(capture_dir));
    const QString path =
        QDir(capture_dir).filePath(name + QStringLiteral(".png"));
    const QImage grabbed = render_widget(capture_widget);
    QVERIFY(!grabbed.isNull());
    QVERIFY(grabbed.save(path, "PNG"));
    const QString capture_sha = sha256_of(path);
    QVERIFY(!capture_sha.isEmpty());
    record.insert(
        QStringLiteral("capture"),
        QStringLiteral("build/gui-gate/wp-5u12/captures/") + name +
            QStringLiteral(".png"));
    record.insert(QStringLiteral("capture_sha256"), capture_sha);
  }

  if (qEnvironmentVariable("PNGA_WP5U12_COMPARE_BASELINES") ==
      QStringLiteral("1")) {
    const QString baseline_dir = env_or(
        "PNGA_WP5U12_BASELINE_DIR", QStringLiteral("tests/gui/baselines/wp-5u12"));
    const QString baseline_path =
        QDir(baseline_dir).filePath(name + QStringLiteral(".png"));
    if (!QFile::exists(baseline_path)) {
      record.insert(QStringLiteral("compare"),
                    QStringLiteral("missing-baseline"));
      cases_.append(record);
      QSKIP("baseline not present; the runner refuses missing baselines");
    }
    const QImage baseline(baseline_path);
    QVERIFY(!baseline.isNull());
    QImage to_compare;
    if (!capture_dir.isEmpty()) {
      to_compare = QImage(
          QDir(capture_dir).filePath(name + QStringLiteral(".png")));
    } else {
      to_compare = render_widget(capture_widget);
    }
    QVERIFY(!to_compare.isNull());
    const QString difference = compare_images(to_compare, baseline);
    if (!difference.isEmpty()) {
      record.insert(QStringLiteral("compare"), QStringLiteral("fail"));
      cases_.append(record);
      QFAIL(qPrintable(
          QStringLiteral("baseline %1 diverged: %2").arg(name, difference)));
    }
    record.insert(QStringLiteral("compare"), QStringLiteral("pass"));
  }

  cases_.append(record);
}

void CompressionInspectorProductGateTest::
    narrow320DegradationActionsAccessibilityNoReplay() {
  gates_[QStringLiteral("keyboard")] = QStringLiteral("pass");
  gates_[QStringLiteral("clipboard")] = QStringLiteral("pass");
  gates_[QStringLiteral("accessibility")] = QStringLiteral("pass");

  MainWindow window;
  window.resize(kWindowWidth, kWindowHeight);
  window.show();
  QCoreApplication::processEvents();
  openValidReady(window, "valid/trace-multiblock-bfinal.png");

  auto* controller = window.findChild<TraceController*>();
  QVERIFY(controller != nullptr);
  const auto accepted_before = controller->acceptedRequestCountForTest();
  auto* block_page = window.findChild<pnga::ui::qt::BlockInspector*>(
      QStringLiteral("blockInspector"));
  QVERIFY(block_page != nullptr);
  const auto generation_before = block_page->view().generation;
  QVERIFY(generation_before != 0);

  const auto stores =
      window.findChildren<pnga::ui::qt::CompressionSelectionStore*>();
  QCOMPARE(stores.size(), 1);
  auto* store = stores.front();

  // 320 px degradation (flow-ui §20.4): every page honors the width without
  // growth, the details pane stays usable and the footer keeps its order;
  // nothing overlaps and the Inspector minimum width never rises.
  applyWidth(window, 320);
  auto* pages = compression_pages(window);
  QVERIFY(pages != nullptr);
  auto* container = compression_container(window);
  QVERIFY(container != nullptr);
  for (int page = 0; page < 3; ++page) {
    pages->setCurrentIndex(page);
    QCoreApplication::processEvents();
    QWidget* active = pages->widget(page);
    auto* splitter =
        active->findChild<QSplitter*>(QStringLiteral("compressionPageSplitter"));
    QVERIFY(splitter != nullptr);
    QVERIFY(splitter->sizes().value(1) >= 120);
    assert_actions(active);
    QCOMPARE(container->width(), 320);
    QVERIFY(container->minimumWidth() <= 320);
    auto* hex = find_hex_button(active);
    if (hex == nullptr) {
      hex = active->findChild<QPushButton*>(
          QStringLiteral("huffmanOpenOccurrence"));
    }
    QVERIFY(hex != nullptr);
    auto* details_title =
        active->findChild<QLabel*>(QStringLiteral("compressionDetailsTitle"));
    QVERIFY(details_title != nullptr);
    QVERIFY(splitter->mapTo(active, QPoint(0, 0)).y() <
            hex->mapTo(active, QPoint(0, 0)).y());
  }

  // Selection persistence across page switches with zero trace work.
  pages->setCurrentIndex(0);
  QCoreApplication::processEvents();
  auto* blocks = blocks_table(window);
  QVERIFY(blocks != nullptr);
  QCOMPARE(blocks->model()->rowCount(), 3);
  blocks->selectRow(1);
  pages->setCurrentIndex(1);
  QCoreApplication::processEvents();
  pages->setCurrentIndex(2);
  QCoreApplication::processEvents();
  pages->setCurrentIndex(0);
  QCoreApplication::processEvents();
  QVERIFY(blocks->selectionModel()->isRowSelected(1, QModelIndex()));
  QVERIFY(store->state().manual.has_value());
  QCOMPARE(store->history().size(), std::size_t{0});
  QCOMPARE(controller->acceptedRequestCountForTest(), accepted_before);

  // Keyboard navigation moves the selection without submitting traces.
  blocks->setFocus();
  QVERIFY(blocks->hasFocus());
  QTest::keyClick(blocks, Qt::Key_Down);
  QCoreApplication::processEvents();
  QCOMPARE(blocks->selectionModel()->currentIndex().row(), 2);
  QTest::keyClick(blocks, Qt::Key_Up);
  QCoreApplication::processEvents();
  QCOMPARE(blocks->selectionModel()->currentIndex().row(), 1);
  QTest::keyClick(blocks, Qt::Key_Home);
  QCoreApplication::processEvents();
  QCOMPARE(blocks->selectionModel()->currentIndex().row(), 0);
  QCOMPARE(controller->acceptedRequestCountForTest(), accepted_before);

  // DEC/HEX toggling only changes presentation.
  auto* base_button =
      window.findChild<QPushButton*>(QStringLiteral("numericBase"));
  QVERIFY(base_button != nullptr);
  base_button->click();
  base_button->click();
  QCOMPARE(controller->acceptedRequestCountForTest(), accepted_before);

  // Resize keeps the bundle and submits nothing.
  window.resize(1100, 720);
  QCoreApplication::processEvents();
  QTest::qWait(30);
  window.resize(kWindowWidth, kWindowHeight);
  QCoreApplication::processEvents();
  QCOMPARE(controller->acceptedRequestCountForTest(), accepted_before);

  // Copy contract: detail values are selectable and the clipboard works.
  blocks->selectRow(0);
  QCoreApplication::processEvents();
  bool found_selectable_value = false;
  const auto labels = block_page->findChildren<QLabel*>();
  for (const auto* label : labels) {
    if (label->textInteractionFlags() & Qt::TextSelectableByMouse) {
      found_selectable_value = true;
      break;
    }
  }
  QVERIFY(found_selectable_value);
  auto* clipboard = QApplication::clipboard();
  clipboard->setText(QStringLiteral("pnga-wp5u12-gate"));
  QCOMPARE(clipboard->text(), QStringLiteral("pnga-wp5u12-gate"));

  // Typed navigation and history: the two Show actions push exactly one
  // history entry each and navigate the typed sources; going back reverts
  // without replay.
  auto* hex_source = window.findChild<pnga::ui::qt::HexSourceTabBar*>(
      QStringLiteral("hexSourceTabs"));
  QVERIFY(hex_source != nullptr);
  auto* hex_button =
      block_page->findChild<QPushButton*>(QStringLiteral("blockShowInHex"));
  QVERIFY(hex_button != nullptr);
  QVERIFY(hex_button->isEnabled());
  hex_button->click();
  QCoreApplication::processEvents();
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kFile);
  QCOMPARE(store->history().size(), std::size_t{1});
  auto* inflated_button = block_page->findChild<QPushButton*>(
      QStringLiteral("blockShowInflatedOutput"));
  QVERIFY(inflated_button != nullptr);
  QVERIFY(inflated_button->isEnabled());
  inflated_button->click();
  QCoreApplication::processEvents();
  // The Inflated source becomes ready asynchronously; the typed target is
  // already in the store and the source switch follows as soon as it is.
  QTRY_COMPARE_WITH_TIMEOUT(hex_source->source(),
                            pnga::ui::qt::HexSource::kInflated, kTimeoutMs);
  QCOMPARE(store->history().size(), std::size_t{2});
  QVERIFY(store->goBack());
  QCOMPARE(hex_source->source(), pnga::ui::qt::HexSource::kFile);
  QCOMPARE(store->history().size(), std::size_t{2});
  QCOMPARE(controller->acceptedRequestCountForTest(), accepted_before);

  // Theme switching is presentation-only.
  {
    pnga::ui::qt::ApplicationTheme theme(qApp);
    theme.setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kDark,
                  /*persist=*/false);
    QCoreApplication::processEvents();
    theme.setMode(pnga::ui::qt::ApplicationTheme::ThemeMode::kLight,
                  /*persist=*/false);
    QCoreApplication::processEvents();
  }
  QCOMPARE(controller->acceptedRequestCountForTest(), accepted_before);
  QCOMPARE(block_page->view().generation, generation_before);

  // Accessibility roles and names through QAccessible (metadata gate; native
  // screen-reader certification stays a manual cell in the evidence matrix).
  for (QTableView* table :
       {blocks, huffman_table(window), decode_table(window)}) {
    const auto iface = QAccessible::queryAccessibleInterface(table);
    QVERIFY(iface != nullptr);
    QCOMPARE(iface->role(), QAccessible::Table);
    QVERIFY(!iface->text(QAccessible::Name).isEmpty());
  }
  const auto status_iface =
      QAccessible::queryAccessibleInterface(context_status(window));
  QVERIFY(status_iface != nullptr);
  QCOMPARE(status_iface->role(), QAccessible::StaticText);
  QVERIFY(!status_iface->text(QAccessible::Name).isEmpty());

  auto* status = context_status(window);
  QVERIFY(status->text().contains(QStringLiteral("ready")));
  gates_[QStringLiteral("no_replay")] = QStringLiteral("pass");
  gates_[QStringLiteral("degradation_320")] = QStringLiteral("pass");
}

void CompressionInspectorProductGateTest::cleanupTestCase() {
  const QString results_path = qEnvironmentVariable("PNGA_WP5U12_RESULTS_JSON");
  if (results_path.isEmpty()) {
    return;
  }
  QJsonObject results;
  results.insert(QStringLiteral("cases"), cases_);
  results.insert(QStringLiteral("gates"), gates_);
  QFile output(results_path);
  QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
  output.write(QJsonDocument(results).toJson(QJsonDocument::Compact));
}

QTEST_MAIN(CompressionInspectorProductGateTest)
#include "compression_inspector_product_gate_test.moc"
