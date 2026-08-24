#include <pnga/ui/qt/application_theme.h>

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QVariant>

#include <algorithm>

int qInitResources_png_analyzer_ui();

namespace pnga::ui::qt {

namespace {

constexpr auto kThemeSettingsKey = "appearance/theme";
constexpr auto kMonospaceProperty = "pnga.theme.monospaceFont";

QString colorPropertyName(ApplicationTheme::ColorToken token) {
  return QStringLiteral("pnga.theme.color.%1")
      .arg(static_cast<int>(token));
}

QColor colorFromHex(const char* value) { return QColor(QString::fromLatin1(value)); }

}  // namespace

ApplicationTheme::ApplicationTheme(QApplication* application, QObject* parent)
    : QObject(parent), application_(application) {
  ::qInitResources_png_analyzer_ui();
}

QString ApplicationTheme::serializeMode(ThemeMode mode) {
  switch (mode) {
    case ThemeMode::kLight:
      return QStringLiteral("light");
    case ThemeMode::kDark:
      return QStringLiteral("dark");
    case ThemeMode::kSystem:
    default:
      return QStringLiteral("system");
  }
}

ApplicationTheme::ThemeMode ApplicationTheme::parseMode(
    const QString& value) noexcept {
  const QString normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("light")) {
    return ThemeMode::kLight;
  }
  if (normalized == QStringLiteral("dark")) {
    return ThemeMode::kDark;
  }
  return ThemeMode::kSystem;
}

ApplicationTheme::ThemeMode ApplicationTheme::readMode(
    const QSettings& settings) {
  return parseMode(settings.value(QString::fromLatin1(kThemeSettingsKey),
                                  QStringLiteral("system"))
                       .toString());
}

void ApplicationTheme::writeMode(QSettings& settings, ThemeMode mode) {
  settings.setValue(QString::fromLatin1(kThemeSettingsKey),
                    serializeMode(mode));
  settings.sync();
}

bool ApplicationTheme::install() {
  if (application_ == nullptr) {
    diagnostic_ = QStringLiteral("theme controller has no QApplication");
    return false;
  }

  // The UI library is static in the desktop build; explicitly initialize its
  // dedicated resource so the stylesheet is available even when the linker
  // does not retain an otherwise unreferenced RCC object.
  ::qInitResources_png_analyzer_ui();
  requested_mode_ = readMode(QSettings());
  if (!style_hint_connected_ && application_->styleHints() != nullptr) {
    style_hint_connected_ = true;
    connect(application_->styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
              if (requested_mode_ == ThemeMode::kSystem) {
                applyMode(requested_mode_);
              }
            });
  }
  return applyMode(requested_mode_);
}

bool ApplicationTheme::setMode(ThemeMode mode, bool persist) {
  if (application_ == nullptr) {
    diagnostic_ = QStringLiteral("theme controller has no QApplication");
    return false;
  }
  requested_mode_ = mode;
  if (persist) {
    QSettings settings;
    writeMode(settings, requested_mode_);
  }
  return applyMode(requested_mode_);
}

ApplicationTheme::ThemeMode ApplicationTheme::resolveEffectiveMode() const {
  if (requested_mode_ != ThemeMode::kSystem) {
    return requested_mode_;
  }
  if (application_ != nullptr && application_->styleHints() != nullptr) {
    switch (application_->styleHints()->colorScheme()) {
      case Qt::ColorScheme::Dark:
        return ThemeMode::kDark;
      case Qt::ColorScheme::Light:
        return ThemeMode::kLight;
      case Qt::ColorScheme::Unknown:
      default:
        break;
    }
  }
  if (application_ != nullptr) {
    return application_->palette().color(QPalette::Window).lightness() < 128
               ? ThemeMode::kDark
               : ThemeMode::kLight;
  }
  return ThemeMode::kLight;
}

void ApplicationTheme::installBaseStyle() {
  if (base_style_installed_ || application_ == nullptr) {
    return;
  }
#if defined(Q_OS_MACOS)
  // Preserve the native macOS style and its platform control metrics.
  base_style_installed_ = true;
#else
  if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"));
      fusion != nullptr) {
    application_->setStyle(fusion);
    base_style_installed_ = true;
  } else {
    diagnostic_ = QStringLiteral("Fusion style unavailable; using platform style");
  }
#endif
}

void ApplicationTheme::refreshFonts() {
  ui_font_ = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  if (ui_font_.pointSizeF() > 0.0 && ui_font_.pointSizeF() < 9.0) {
    ui_font_.setPointSizeF(9.0);
  }

  QStringList candidates;
#if defined(Q_OS_WIN)
  candidates = {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")};
#elif defined(Q_OS_MACOS)
  candidates = {QStringLiteral("SF Mono"), QStringLiteral("Menlo"),
                QStringLiteral("Monaco")};
#else
  candidates = {QStringLiteral("Noto Sans Mono"),
                QStringLiteral("DejaVu Sans Mono")};
#endif

  const QStringList installed = QFontDatabase::families();
  for (const QString& family : candidates) {
    if (!installed.contains(family, Qt::CaseInsensitive)) {
      continue;
    }
    QFont candidate(family, std::max(1, ui_font_.pointSize()));
    candidate.setStyleHint(QFont::Monospace);
    if (QFontInfo(candidate).fixedPitch()) {
      monospace_font_ = candidate;
      break;
    }
  }
  if (monospace_font_.family().isEmpty() ||
      !QFontInfo(monospace_font_).fixedPitch()) {
    monospace_font_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monospace_font_.setStyleHint(QFont::Monospace);
  }
  if (monospace_font_.pointSizeF() <= 0.0) {
    monospace_font_.setPointSizeF(ui_font_.pointSizeF());
  }
  application_->setFont(ui_font_);
  application_->setProperty(kMonospaceProperty,
                             QVariant::fromValue(monospace_font_));
}

void ApplicationTheme::refreshTokens() {
  if (effective_mode_ == ThemeMode::kDark) {
    tokens_.window = colorFromHex("#0F172A");
    tokens_.base = colorFromHex("#111827");
    tokens_.raised = colorFromHex("#1E293B");
    tokens_.text = colorFromHex("#F8FAFC");
    tokens_.muted_text = colorFromHex("#CBD5E1");
    tokens_.border = colorFromHex("#475569");
    tokens_.accent = colorFromHex("#60A5FA");
    tokens_.accent_hover = colorFromHex("#93C5FD");
    tokens_.accent_pressed = colorFromHex("#3B82F6");
    tokens_.accent_text = colorFromHex("#0F172A");
    tokens_.focus_ring = colorFromHex("#93C5FD");
    tokens_.disabled_text = colorFromHex("#64748B");
    tokens_.disabled_surface = colorFromHex("#1E293B");
    tokens_.current_pixel = colorFromHex("#7F1D1D");
    tokens_.dependency = colorFromHex("#78350F");
    tokens_.neutral = colorFromHex("#1E3A4A");
  } else {
    tokens_.window = colorFromHex("#F8FAFC");
    tokens_.base = colorFromHex("#FFFFFF");
    tokens_.raised = colorFromHex("#F1F5F9");
    tokens_.text = colorFromHex("#0F172A");
    tokens_.muted_text = colorFromHex("#475569");
    tokens_.border = colorFromHex("#CBD5E1");
    tokens_.accent = colorFromHex("#2563EB");
    tokens_.accent_hover = colorFromHex("#1D4ED8");
    tokens_.accent_pressed = colorFromHex("#1E40AF");
    tokens_.accent_text = colorFromHex("#FFFFFF");
    tokens_.focus_ring = colorFromHex("#1D4ED8");
    tokens_.disabled_text = colorFromHex("#94A3B8");
    tokens_.disabled_surface = colorFromHex("#E2E8F0");
    tokens_.current_pixel = colorFromHex("#FECACA");
    tokens_.dependency = colorFromHex("#FEF3C7");
    tokens_.neutral = colorFromHex("#CCFBF1");
  }
}

QString ApplicationTheme::expandedStylesheet(bool* ok) {
  if (ok != nullptr) {
    *ok = false;
  }
  QFile resource(QStringLiteral(":/pnga/theme/application.qss"));
  if (!resource.open(QIODevice::ReadOnly | QIODevice::Text)) {
    diagnostic_ = QStringLiteral("theme stylesheet resource unavailable");
    return {};
  }
  QString stylesheet = QString::fromUtf8(resource.readAll());
  const QList<QPair<QString, QColor>> substitutions = {
      {QStringLiteral("@WINDOW@"), tokens_.window},
      {QStringLiteral("@BASE@"), tokens_.base},
      {QStringLiteral("@RAISED@"), tokens_.raised},
      {QStringLiteral("@TEXT@"), tokens_.text},
      {QStringLiteral("@MUTED_TEXT@"), tokens_.muted_text},
      {QStringLiteral("@BORDER@"), tokens_.border},
      {QStringLiteral("@ACCENT@"), tokens_.accent},
      {QStringLiteral("@ACCENT_HOVER@"), tokens_.accent_hover},
      {QStringLiteral("@ACCENT_PRESSED@"), tokens_.accent_pressed},
      {QStringLiteral("@ACCENT_TEXT@"), tokens_.accent_text},
      {QStringLiteral("@FOCUS_RING@"), tokens_.focus_ring},
  };
  for (const auto& substitution : substitutions) {
    stylesheet.replace(substitution.first,
                       substitution.second.name(QColor::HexRgb));
  }
  if (stylesheet.contains(QLatin1Char('@'))) {
    diagnostic_ = QStringLiteral("theme stylesheet has unresolved token");
    return {};
  }
  if (ok != nullptr) {
    *ok = true;
  }
  return stylesheet;
}

bool ApplicationTheme::applyMode(ThemeMode mode) {
  if (application_ == nullptr) {
    return false;
  }
  requested_mode_ = mode;
  installBaseStyle();
  effective_mode_ = resolveEffectiveMode();
  refreshFonts();
  refreshTokens();

  QPalette palette = application_->palette();
  palette.setColor(QPalette::Window, tokens_.window);
  palette.setColor(QPalette::Base, tokens_.base);
  palette.setColor(QPalette::AlternateBase, tokens_.raised);
  palette.setColor(QPalette::Text, tokens_.text);
  palette.setColor(QPalette::WindowText, tokens_.text);
  palette.setColor(QPalette::Button, tokens_.raised);
  palette.setColor(QPalette::ButtonText, tokens_.text);
  palette.setColor(QPalette::Highlight, tokens_.accent);
  palette.setColor(QPalette::HighlightedText, tokens_.accent_text);
  palette.setColor(QPalette::Disabled, QPalette::Text, tokens_.disabled_text);
  palette.setColor(QPalette::Disabled, QPalette::WindowText,
                   tokens_.disabled_text);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                   tokens_.disabled_text);
  application_->setPalette(palette);

  for (int index = 0; index <= static_cast<int>(ColorToken::kNeutral); ++index) {
    const auto token = static_cast<ColorToken>(index);
    application_->setProperty(colorPropertyName(token).toLatin1().constData(),
                              color(token));
  }

  bool stylesheet_ok = false;
  const QString stylesheet = expandedStylesheet(&stylesheet_ok);
  if (stylesheet_ok) {
    application_->setStyleSheet(stylesheet);
  } else {
    application_->setStyleSheet(QString());
  }
  emit themeChanged();
  return stylesheet_ok;
}

QColor ApplicationTheme::color(ColorToken token) const {
  switch (token) {
    case ColorToken::kWindow:
      return tokens_.window;
    case ColorToken::kBase:
      return tokens_.base;
    case ColorToken::kRaised:
      return tokens_.raised;
    case ColorToken::kText:
      return tokens_.text;
    case ColorToken::kMutedText:
      return tokens_.muted_text;
    case ColorToken::kBorder:
      return tokens_.border;
    case ColorToken::kAccent:
      return tokens_.accent;
    case ColorToken::kAccentHover:
      return tokens_.accent_hover;
    case ColorToken::kAccentPressed:
      return tokens_.accent_pressed;
    case ColorToken::kAccentText:
      return tokens_.accent_text;
    case ColorToken::kFocusRing:
      return tokens_.focus_ring;
    case ColorToken::kDisabledText:
      return tokens_.disabled_text;
    case ColorToken::kDisabledSurface:
      return tokens_.disabled_surface;
    case ColorToken::kCurrentPixel:
      return tokens_.current_pixel;
    case ColorToken::kDependency:
      return tokens_.dependency;
    case ColorToken::kNeutral:
      return tokens_.neutral;
  }
  return {};
}

QFont ApplicationTheme::applicationMonospaceFont() {
  if (qApp == nullptr) {
    return QFont();
  }
  const QVariant value = qApp->property(kMonospaceProperty);
  if (value.isValid()) {
    return value.value<QFont>();
  }
  QFont fallback = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  fallback.setStyleHint(QFont::Monospace);
  return fallback;
}

QColor ApplicationTheme::applicationColor(ColorToken token) {
  if (qApp == nullptr) {
    return {};
  }
  return qApp->property(colorPropertyName(token).toLatin1().constData())
      .value<QColor>();
}

}  // namespace pnga::ui::qt
