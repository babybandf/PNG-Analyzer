// WP-699-706 product gate (T17). Executes the frozen end-to-end acceptance
// flow — open → default frame 0 paused → playback/seek → four stage views →
// physical Hex sources → partial playback disabled → static fallback → close
// → static isolation — against the real MainWindow. The assertions run under
// every platform, including QT_QPA_PLATFORM=offscreen in the regular dev and
// ASan suites. When PNGA_APNG_GATE_OUT points at a directory the run also
// writes one QWidget::grab() PNG per cell plus one pnga-apng-product-gate-v1
// evidence record with environment fields, so a native macOS window capture
// can be produced by invoking this binary without the offscreen override and
// with the output variable set. Offscreen runs without the variable never
// write files and stay deterministic.

#include "main_window.h"

#include "apng_fixture.h"

#include <pnga/png-format/animation_index.h>
#include <pnga/ui/qt/animation_inspector.h>
#include <pnga/ui/qt/animation_timeline.h>
#include <pnga/ui/qt/delivered_image_view.h>
#include <pnga/ui/qt/hex_source_tab_bar.h>
#include <pnga/ui/qt/selection_bus.h>

#include <QtTest/QtTest>

#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QSysInfo>
#include <QTabWidget>
#include <QTemporaryDir>

#include <array>
#include <variant>
#include <vector>

namespace {

using pnga::png_format::FrameControl;

QString bytes_sha256(const std::vector<std::byte>& bytes) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(reinterpret_cast<const char*>(bytes.data()),
               static_cast<qsizetype>(bytes.size()));
  return QString::fromLatin1(hash.result().toHex());
}

bool write_sample(const QString& path, const std::vector<std::byte>& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const auto written = file.write(
      reinterpret_cast<const char*>(bytes.data()),
      static_cast<qint64>(bytes.size()));
  file.flush();
  return written == static_cast<qint64>(bytes.size());
}

FrameControl frame_control(std::uint16_t delay_num, std::uint8_t dispose) {
  FrameControl control{};
  control.sequence = 0;
  control.width = 96;
  control.height = 64;
  control.x = 0;
  control.y = 0;
  control.delay_num = delay_num;
  control.delay_den = 100;
  control.dispose = dispose;
  control.blend = 0;
  return control;
}

class ApngProductGateTest final : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();

  // Cell 1: open a valid APNG → mounts animation UI, default frame 0, paused.
  void validApngOpensPausedOnFrame0();

  // Cell 2: next/first navigation and real-time play/pause.
  void navigationPlaybackAndFirstStage();

  // Cell 3: four stage views show distinct canvas stages; Hex follows the
  // frame stream; static fallback returns to IDAT with a StaticImage identity.
  void fourStagesHexAndFallback();

  // Cell 4: a partial APNG mounts controls, keeps verified frame 0 viewable
  // and keeps playback disabled.
  void partialApngDisablesPlayback();

  // Cell 5: close destroys every APNG widget; a static PNG never creates any.
  void closeAndStaticIsolation();

  // Cell 6: a fully transparent canvas stage shows an explanatory hint that
  // disappears once the canvas carries content.
  void emptyCanvasHintExplainsTransparentCanvas();

 private:
  void openApng(MainWindow& window, QTemporaryDir& dir,
                const std::vector<std::byte>& bytes, const QString& name);
  static pnga::ui::qt::DeliveredImageView* stageView(MainWindow& window,
                                                     int index);
  void capture(MainWindow& window, const QString& cell);
  void write_record();

  QJsonObject environment_;
  QString output_dir_;
  std::vector<std::byte> valid_sample_;
  std::vector<std::byte> partial_sample_;
  std::vector<std::byte> static_sample_;
  QJsonArray cells_;
};

void ApngProductGateTest::initTestCase() {
  const std::array<std::array<std::byte, 4>, 3> colors = {
      std::array{std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}},
      std::array{std::byte{0}, std::byte{255}, std::byte{0}, std::byte{255}},
      std::array{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{255}}};
  const std::array<FrameControl, 3> frames = {
      frame_control(1, 0),   // red, 0.5 s, dispose NONE
      frame_control(1, 1),   // green, dispose BACKGROUND
      frame_control(1, 0),   // blue, dispose NONE
  };
  valid_sample_ = pnga_test::make_apng_canvas(false, frames, colors, 96, 64);

  // Partial: verified prefix of one complete frame, then a truncated fdAT
  // envelope (IEND and the fdAT tail are cut, so the second frame can never
  // verify). The capability is partial with at least one verified frame.
  const std::vector<std::byte> full = pnga_test::make_apng_canvas(
      false, std::span(frames).first<2>(), std::span(colors).first<2>(), 96,
      64);
  partial_sample_ = full;
  partial_sample_.resize(partial_sample_.size() - 24);

  static_sample_ = [] {
    std::vector<std::byte> png(pnga::png_format::kPngSignature.begin(),
                               pnga::png_format::kPngSignature.end());
    std::vector<std::byte> ihdr;
    pnga_test::append_u32(ihdr, 96);
    pnga_test::append_u32(ihdr, 64);
    ihdr.insert(ihdr.end(), {std::byte{8}, std::byte{6}, std::byte{0},
                                std::byte{0}, std::byte{0}});
    pnga_test::append_apng_chunk(png, "IHDR", ihdr);
    pnga_test::append_apng_chunk(
        png, "IDAT", pnga_test::apng_zlib_payload_canvas(96, 64, {}));
    pnga_test::append_apng_chunk(png, "IEND", {});
    return png;
  }();

  output_dir_ = qEnvironmentVariable("PNGA_APNG_GATE_OUT");
  environment_ = QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("pnga-apng-product-gate-v1")},
      {QStringLiteral("os"),
       QSysInfo::productType() + QLatin1Char(' ') + QSysInfo::productVersion()},
      {QStringLiteral("arch"), QSysInfo::currentCpuArchitecture()},
      {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
      {QStringLiteral("qpa_platform"), QGuiApplication::platformName()},
      {QStringLiteral("offscreen"),
       QGuiApplication::platformName() == QStringLiteral("offscreen")},
      {QStringLiteral("timestamp_utc"),
       QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
      {QStringLiteral("git_commit"),
       qEnvironmentVariable("PNGA_APNG_GATE_COMMIT")},
      {QStringLiteral("samples"),
       QJsonObject{
           {QStringLiteral("valid_sha256"), bytes_sha256(valid_sample_)},
           {QStringLiteral("partial_sha256"), bytes_sha256(partial_sample_)},
           {QStringLiteral("static_sha256"), bytes_sha256(static_sample_)}}},
  };
  const auto* screen = QGuiApplication::primaryScreen();
  if (screen != nullptr) {
    environment_.insert(QStringLiteral("device_pixel_ratio"),
                        screen->devicePixelRatio());
    environment_.insert(QStringLiteral("logical_dpi"),
                        screen->logicalDotsPerInch());
  }

  if (!output_dir_.isEmpty()) {
    QDir().mkpath(output_dir_ + QStringLiteral("/captures"));
  }
}

void ApngProductGateTest::openApng(MainWindow& window, QTemporaryDir& dir,
                                   const std::vector<std::byte>& bytes,
                                   const QString& name) {
  window.resize(1400, 950);
  const QString path = dir.filePath(name);
  QVERIFY2(write_sample(path, bytes), "sample write failed");
  QVERIFY2(window.openFile(path), "openFile failed");
}

pnga::ui::qt::DeliveredImageView* ApngProductGateTest::stageView(
    MainWindow& window, int index) {
  return window.findChild<pnga::ui::qt::DeliveredImageView*>(
      QStringLiteral("animationStage%1").arg(index));
}

void ApngProductGateTest::capture(MainWindow& window, const QString& cell) {
  QJsonObject entry{
      {QStringLiteral("cell"), cell},
      {QStringLiteral("result"), QStringLiteral("captured")},
      {QStringLiteral("window_size"),
       QString::number(window.width()) + QLatin1Char('x') +
           QString::number(window.height())},
  };
  if (!output_dir_.isEmpty()) {
    const QString path = output_dir_ + QStringLiteral("/captures/") + cell +
                         QStringLiteral(".png");
    const QPixmap grabbed = window.grab();
    QVERIFY2(grabbed.save(path, "PNG"), qPrintable(path));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    entry.insert(QStringLiteral("capture_png"),
                 QStringLiteral("captures/") + cell + QStringLiteral(".png"));
    entry.insert(QStringLiteral("capture_png_sha256"),
                 QString::fromLatin1(
                     QCryptographicHash::hash(saved.readAll(),
                                              QCryptographicHash::Sha256)
                         .toHex()));
  }
  cells_.append(entry);
}

void ApngProductGateTest::write_record() {
  if (output_dir_.isEmpty()) {
    return;
  }
  environment_.insert(QStringLiteral("cells"), cells_);
  QFile record(output_dir_ +
               QStringLiteral("/apng-product-gate-evidence.json"));
  QVERIFY2(record.open(QIODevice::WriteOnly | QIODevice::Truncate),
           "evidence record write failed");
  record.write(QJsonDocument(environment_).toJson(QJsonDocument::Indented));
  record.close();
}

void ApngProductGateTest::validApngOpensPausedOnFrame0() {
  QTemporaryDir dir;
  MainWindow window;
  openApng(window, dir, valid_sample_, QStringLiteral("valid.apng"));
  window.show();

  QTRY_VERIFY_WITH_TIMEOUT(stageView(window, 0) != nullptr, 4000);
  auto* output = stageView(window, 0);
  QTRY_VERIFY_WITH_TIMEOUT(!output->image().isNull(), 4000);
  QCOMPARE(output->image().pixelColor(0, 0), QColor(Qt::red));

  auto* frame = window.findChild<QSpinBox*>(QStringLiteral("animationFrame"));
  auto* play = window.findChild<QPushButton*>(QStringLiteral("animationPlay"));
  auto* list = window.findChild<QListView*>(QStringLiteral("animationThumbnails"));
  auto* status = window.findChild<QLabel*>(QStringLiteral("animationStatus"));
  QVERIFY(frame != nullptr);
  QVERIFY(play != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(status != nullptr);
  QCOMPARE(frame->value(), 0);
  QCOMPARE(list->model()->rowCount(), 3);
  // Thumbnails carry a thin black outline once the async worker delivers
  // them; the first pixel must be pure black.
  QTRY_COMPARE_WITH_TIMEOUT(
      qvariant_cast<QImage>(list->model()->data(list->model()->index(0, 0),
                                                Qt::DecorationRole))
          .pixelColor(0, 0),
      QColor(Qt::black), 4000);
  QVERIFY(play->isEnabled());
  QCOMPARE(play->text(), QStringLiteral("Play"));
  QVERIFY(status->text().contains(QStringLiteral("Paused")));

  auto* session = window.findChild<DocumentSession*>();
  auto* controller = window.findChild<AnimationController*>();
  QVERIFY(session != nullptr);
  QVERIFY(controller != nullptr);
  QCOMPARE(controller->capability(), AnimationController::Capability::kValid);
  QTRY_COMPARE_WITH_TIMEOUT(controller->generation(), session->generation(),
                            4000);

  capture(window, QStringLiteral("valid-open-frame0-paused"));
  write_record();
}

void ApngProductGateTest::navigationPlaybackAndFirstStage() {
  QTemporaryDir dir;
  MainWindow window;
  openApng(window, dir, valid_sample_, QStringLiteral("valid.apng"));
  window.show();
  QTRY_VERIFY_WITH_TIMEOUT(stageView(window, 0) != nullptr, 4000);
  auto* output = stageView(window, 0);
  QTRY_VERIFY_WITH_TIMEOUT(!output->image().isNull(), 4000);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  auto* frame = window.findChild<QSpinBox*>(QStringLiteral("animationFrame"));
  auto* play = window.findChild<QPushButton*>(QStringLiteral("animationPlay"));
  QVERIFY(tabs != nullptr);

  window.findChild<QPushButton*>(QStringLiteral("animationNext"))->click();
  QTRY_COMPARE_WITH_TIMEOUT(output->image().pixelColor(0, 0),
                            QColor(Qt::green), 4000);
  QCOMPARE(frame->value(), 1);
  window.findChild<QPushButton*>(QStringLiteral("animationFirst"))->click();
  QTRY_COMPARE_WITH_TIMEOUT(output->image().pixelColor(0, 0), QColor(Qt::red),
                            4000);
  QCOMPARE(frame->value(), 0);

  // Playback switches to the Post-Blend stage and advances frames in real
  // time; pausing returns to manual navigation on frame 0.
  play->click();
  QCOMPARE(tabs->currentIndex(), 6);
  QCOMPARE(play->text(), QStringLiteral("Pause"));
  auto* post = stageView(window, 2);
  QTRY_VERIFY_WITH_TIMEOUT(!post->image().isNull(), 4000);
  QTRY_COMPARE_WITH_TIMEOUT(frame->value(), 1, 4000);
  play->click();
  QCOMPARE(play->text(), QStringLiteral("Play"));
  window.findChild<QPushButton*>(QStringLiteral("animationFirst"))->click();
  QTRY_COMPARE_WITH_TIMEOUT(frame->value(), 0, 4000);
  QTRY_COMPARE_WITH_TIMEOUT(post->image().pixelColor(0, 0), QColor(Qt::red),
                            4000);

  capture(window, QStringLiteral("navigation-playback-postblend"));
  write_record();
}

void ApngProductGateTest::fourStagesHexAndFallback() {
  QTemporaryDir dir;
  MainWindow window;
  openApng(window, dir, valid_sample_, QStringLiteral("valid.apng"));
  window.show();
  QTRY_VERIFY_WITH_TIMEOUT(stageView(window, 0) != nullptr, 4000);
  QTRY_VERIFY_WITH_TIMEOUT(!stageView(window, 0)->image().isNull(), 4000);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  auto* hex_bar = window.findChild<pnga::ui::qt::HexSourceTabBar*>();
  auto* bus = window.findChild<pnga::ui::qt::SelectionBus*>();
  QVERIFY(tabs != nullptr);
  QVERIFY(hex_bar != nullptr);
  QVERIFY(bus != nullptr);

  window.findChild<QPushButton*>(QStringLiteral("animationNext"))->click();
  auto* output = stageView(window, 0);
  QTRY_COMPARE_WITH_TIMEOUT(output->image().pixelColor(0, 0),
                            QColor(Qt::green), 4000);
  capture(window, QStringLiteral("stage-frame-output"));

  tabs->setCurrentIndex(5);
  auto* pre = stageView(window, 1);
  QTRY_VERIFY_WITH_TIMEOUT(!pre->image().isNull(), 4000);
  // Frame 0 used dispose NONE, so the Pre-Blend canvas of frame 1 is red.
  QCOMPARE(pre->image().pixelColor(0, 0), QColor(Qt::red));

  tabs->setCurrentIndex(6);
  auto* post = stageView(window, 2);
  QTRY_VERIFY_WITH_TIMEOUT(!post->image().isNull(), 4000);
  QCOMPARE(post->image().pixelColor(0, 0), QColor(Qt::green));

  tabs->setCurrentIndex(7);
  auto* disposed = stageView(window, 3);
  QTRY_VERIFY_WITH_TIMEOUT(!disposed->image().isNull(), 4000);
  // Frame 1 dispose BACKGROUND clears to transparent black.
  QCOMPARE(disposed->image().pixelColor(0, 0).alpha(), 0);
  capture(window, QStringLiteral("stage-post-dispose"));

  QCOMPARE(hex_bar->tabText(0), QStringLiteral("File"));
  QCOMPARE(hex_bar->tabText(1), QStringLiteral("Frame Stream"));
  tabs->setCurrentIndex(0);
  QCOMPARE(hex_bar->tabText(1), QStringLiteral("IDAT"));
  tabs->setCurrentIndex(4);
  // The frame worker result arrives asynchronously before the Hex source
  // follows the frame stream again.
  QTRY_COMPARE_WITH_TIMEOUT(hex_bar->tabText(1),
                            QStringLiteral("Frame Stream"), 4000);

  auto* fallback =
      window.findChild<QPushButton*>(QStringLiteral("animationFallback"));
  QVERIFY(fallback != nullptr);
  QVERIFY(fallback->isVisible());
  fallback->click();
  QCOMPARE(hex_bar->tabText(1), QStringLiteral("IDAT"));
  const auto selected = bus->current();
  QVERIFY(selected.image.has_value());
  QVERIFY(std::holds_alternative<pnga::trace_model::StaticImage>(
      selected.image->identity));
  capture(window, QStringLiteral("static-fallback-selected"));

  write_record();
}

void ApngProductGateTest::partialApngDisablesPlayback() {
  QTemporaryDir dir;
  MainWindow window;
  openApng(window, dir, partial_sample_, QStringLiteral("partial.apng"));
  window.show();

  QTRY_VERIFY_WITH_TIMEOUT(stageView(window, 0) != nullptr, 4000);
  auto* output = stageView(window, 0);
  QTRY_VERIFY_WITH_TIMEOUT(!output->image().isNull(), 4000);
  QCOMPARE(output->image().pixelColor(0, 0), QColor(Qt::red));

  auto* controller = window.findChild<AnimationController*>();
  QVERIFY(controller != nullptr);
  QCOMPARE(controller->capability(), AnimationController::Capability::kPartial);
  auto* play = window.findChild<QPushButton*>(QStringLiteral("animationPlay"));
  auto* status = window.findChild<QLabel*>(QStringLiteral("animationStatus"));
  QVERIFY(play != nullptr);
  QVERIFY(!play->isEnabled());
  QVERIFY(status->text().contains(QStringLiteral("Partial")));
  QVERIFY(window.findChild<pnga::ui::qt::AnimationTimelineWidget*>() !=
          nullptr);
  capture(window, QStringLiteral("partial-playback-disabled"));
  write_record();
}

void ApngProductGateTest::closeAndStaticIsolation() {
  {
    QTemporaryDir dir;
    MainWindow window;
    openApng(window, dir, valid_sample_, QStringLiteral("valid.apng"));
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(stageView(window, 0) != nullptr, 4000);
    QMetaObject::invokeMethod(&window, "onCloseTriggered",
                              Qt::DirectConnection);
    QVERIFY(window.findChild<pnga::ui::qt::AnimationTimelineWidget*>() ==
            nullptr);
    QVERIFY(window.findChild<pnga::ui::qt::AnimationInspector*>() == nullptr);
    QVERIFY(stageView(window, 0) == nullptr);
  }
  {
    QTemporaryDir dir;
    MainWindow window;
    openApng(window, dir, static_sample_, QStringLiteral("static.png"));
    window.show();
    QTest::qWait(300);
    QVERIFY(window.findChild<pnga::ui::qt::AnimationTimelineWidget*>() ==
            nullptr);
    QVERIFY(window.findChild<pnga::ui::qt::AnimationInspector*>() == nullptr);
    QVERIFY(stageView(window, 0) == nullptr);
    auto* controller = window.findChild<AnimationController*>();
    QVERIFY(controller != nullptr);
    QCOMPARE(controller->capability(), AnimationController::Capability::kStatic);
    auto* hex_bar = window.findChild<pnga::ui::qt::HexSourceTabBar*>();
    QVERIFY(hex_bar != nullptr);
    QCOMPARE(hex_bar->tabText(1), QStringLiteral("IDAT"));
    auto* preview = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
    QVERIFY(preview != nullptr);
    QCOMPARE(preview->count(), 4);
    capture(window, QStringLiteral("static-no-animation-ui"));
  }
  write_record();
}

}  // namespace

void ApngProductGateTest::emptyCanvasHintExplainsTransparentCanvas() {
  QTemporaryDir dir;
  MainWindow window;
  openApng(window, dir, valid_sample_, QStringLiteral("valid.apng"));
  window.show();

  QTRY_VERIFY_WITH_TIMEOUT(stageView(window, 0) != nullptr, 4000);
  QTRY_VERIFY_WITH_TIMEOUT(!stageView(window, 0)->image().isNull(), 4000);
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("previewTabs"));
  QVERIFY(tabs != nullptr);

  // Frame 0 Pre-Blend is the initial transparent canvas: the view must say
  // so instead of looking like a missing image.
  tabs->setCurrentIndex(5);
  auto* pre = stageView(window, 1);
  QTRY_VERIFY_WITH_TIMEOUT(!pre->image().isNull(), 4000);
  auto* hint = pre->findChild<QLabel*>(QStringLiteral("emptyCanvasHint"));
  QVERIFY(hint != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(hint->isVisible(), 4000);
  QVERIFY(hint->text().contains(QStringLiteral("Empty canvas")));
  QVERIFY(hint->text().contains(QStringLiteral("transparent black")));

  // Frame 1 Pre-Blend carries frame 0 (dispose NONE): no hint.
  window.findChild<QPushButton*>(QStringLiteral("animationNext"))->click();
  QTRY_COMPARE_WITH_TIMEOUT(pre->image().pixelColor(0, 0), QColor(Qt::red),
                            4000);
  QVERIFY(!hint->isVisible());
  write_record();
}

QTEST_MAIN(ApngProductGateTest)
#include "apng_product_gate_test.moc"
