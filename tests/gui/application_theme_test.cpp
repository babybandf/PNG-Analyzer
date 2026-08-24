// WP-5U14: theme mode serialization, palette/token application and resource
// expansion contracts. Rendering remains platform-owned; this test checks the
// deterministic product layer installed above it.

#include <pnga/ui/qt/application_theme.h>

#include <QtTest/QtTest>

#include <QApplication>
#include <QFontInfo>
#include <QPalette>
#include <QSettings>
#include <QStyleHints>

using pnga::ui::qt::ApplicationTheme;

class ApplicationThemeTest final : public QObject {
  Q_OBJECT
 private slots:
  void modeSerializationIsStable();
  void settingsRoundTripUsesAppearanceKey();
  void lightAndDarkModesApplySafely();
  void systemModeTracksQtColorScheme();
};

void ApplicationThemeTest::modeSerializationIsStable() {
  QCOMPARE(ApplicationTheme::serializeMode(ApplicationTheme::ThemeMode::kSystem),
           QStringLiteral("system"));
  QCOMPARE(ApplicationTheme::serializeMode(ApplicationTheme::ThemeMode::kLight),
           QStringLiteral("light"));
  QCOMPARE(ApplicationTheme::serializeMode(ApplicationTheme::ThemeMode::kDark),
           QStringLiteral("dark"));
  QCOMPARE(ApplicationTheme::parseMode(QStringLiteral(" LIGHT ")),
           ApplicationTheme::ThemeMode::kLight);
  QCOMPARE(ApplicationTheme::parseMode(QStringLiteral("dark")),
           ApplicationTheme::ThemeMode::kDark);
  QCOMPARE(ApplicationTheme::parseMode(QStringLiteral("future-value")),
           ApplicationTheme::ThemeMode::kSystem);
}

void ApplicationThemeTest::settingsRoundTripUsesAppearanceKey() {
  QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("PNG Analyzer Tests"),
                     QStringLiteral("Application Theme"));
  settings.clear();
  ApplicationTheme::writeMode(settings, ApplicationTheme::ThemeMode::kDark);
  QCOMPARE(ApplicationTheme::readMode(settings),
           ApplicationTheme::ThemeMode::kDark);
  QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(),
           QStringLiteral("dark"));
  settings.clear();
}

void ApplicationThemeTest::lightAndDarkModesApplySafely() {
  ApplicationTheme theme(qApp);
  QVERIFY(theme.setMode(ApplicationTheme::ThemeMode::kLight, false));
  QCOMPARE(theme.requestedMode(), ApplicationTheme::ThemeMode::kLight);
  QCOMPARE(theme.effectiveMode(), ApplicationTheme::ThemeMode::kLight);
  QCOMPARE(theme.color(ApplicationTheme::ColorToken::kAccent),
           QColor(QStringLiteral("#2563EB")));
  QVERIFY(!qApp->styleSheet().isEmpty());
  QVERIFY(!qApp->styleSheet().contains(QStringLiteral("@WINDOW@")));
  QVERIFY(!qApp->styleSheet().contains(QStringLiteral("@ACCENT@")));
  QVERIFY(theme.uiFont().pointSizeF() > 0.0);
  QVERIFY(QFontInfo(theme.monospaceFont()).fixedPitch());

  QVERIFY(theme.setMode(ApplicationTheme::ThemeMode::kDark, false));
  QCOMPARE(theme.effectiveMode(), ApplicationTheme::ThemeMode::kDark);
  QCOMPARE(theme.color(ApplicationTheme::ColorToken::kWindow),
           QColor(QStringLiteral("#0F172A")));
  QVERIFY(theme.color(ApplicationTheme::ColorToken::kAccent).lightness() >
          theme.color(ApplicationTheme::ColorToken::kAccentPressed).lightness());
  QCOMPARE(qApp->palette().color(QPalette::Window),
           QColor(QStringLiteral("#0F172A")));
}

void ApplicationThemeTest::systemModeTracksQtColorScheme() {
  ApplicationTheme theme(qApp);
  QVERIFY(theme.setMode(ApplicationTheme::ThemeMode::kSystem, false));
  switch (qApp->styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:
      QCOMPARE(theme.effectiveMode(), ApplicationTheme::ThemeMode::kDark);
      break;
    case Qt::ColorScheme::Light:
      QCOMPARE(theme.effectiveMode(), ApplicationTheme::ThemeMode::kLight);
      break;
    case Qt::ColorScheme::Unknown:
    default:
      QVERIFY(theme.effectiveMode() == ApplicationTheme::ThemeMode::kLight ||
              theme.effectiveMode() == ApplicationTheme::ThemeMode::kDark);
      break;
  }
}

QTEST_MAIN(ApplicationThemeTest)
#include "application_theme_test.moc"
