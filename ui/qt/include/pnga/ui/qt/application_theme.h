#ifndef PNGA_UI_QT_APPLICATION_THEME_H
#define PNGA_UI_QT_APPLICATION_THEME_H

// WP-5U14: product-owned Qt Widgets theme controller. The controller owns
// palette/style/stylesheet installation and exposes only stable theme tokens
// and fonts to custom-painted widgets.

#include <QObject>

#include <QColor>
#include <QFont>

class QApplication;
class QSettings;

namespace pnga::ui::qt {

class ApplicationTheme final : public QObject {
  Q_OBJECT
 public:
  enum class ThemeMode { kSystem = 0, kLight = 1, kDark = 2 };
  Q_ENUM(ThemeMode)

  enum class ColorToken {
    kWindow,
    kBase,
    kRaised,
    kText,
    kMutedText,
    kBorder,
    kAccent,
    kAccentHover,
    kAccentPressed,
    kAccentText,
    kFocusRing,
    kDisabledText,
    kDisabledSurface,
    kCurrentPixel,
    kDependency,
    kNeutral,
  };

  explicit ApplicationTheme(QApplication* application,
                            QObject* parent = nullptr);

  // Reads appearance/theme and installs the selected mode. The controller
  // must be installed before MainWindow is constructed.
  bool install();

  // Applies immediately. Persistence is explicit so tests and previews can
  // switch modes without mutating the user's settings.
  bool setMode(ThemeMode mode, bool persist = true);

  ThemeMode requestedMode() const noexcept { return requested_mode_; }
  ThemeMode effectiveMode() const noexcept { return effective_mode_; }
  QFont uiFont() const { return ui_font_; }
  QFont monospaceFont() const { return monospace_font_; }
  QColor color(ColorToken token) const;
  QString diagnostic() const { return diagnostic_; }

  // Widgets created after installation can obtain the shared product font
  // and semantic colors without owning the controller. These accessors read
  // the immutable application properties populated during apply().
  static QFont applicationMonospaceFont();
  static QColor applicationColor(ColorToken token);

  static QString serializeMode(ThemeMode mode);
  static ThemeMode parseMode(const QString& value) noexcept;
  static ThemeMode readMode(const QSettings& settings);
  static void writeMode(QSettings& settings, ThemeMode mode);

 signals:
  void themeChanged();

 private:
  struct Tokens {
    QColor window;
    QColor base;
    QColor raised;
    QColor text;
    QColor muted_text;
    QColor border;
    QColor accent;
    QColor accent_hover;
    QColor accent_pressed;
    QColor accent_text;
    QColor focus_ring;
    QColor disabled_text;
    QColor disabled_surface;
    QColor current_pixel;
    QColor dependency;
    QColor neutral;
  };

  bool applyMode(ThemeMode mode);
  ThemeMode resolveEffectiveMode() const;
  void refreshFonts();
  void refreshTokens();
  QString expandedStylesheet(bool* ok);
  void installBaseStyle();

  QApplication* application_ = nullptr;
  ThemeMode requested_mode_ = ThemeMode::kSystem;
  ThemeMode effective_mode_ = ThemeMode::kLight;
  QFont ui_font_;
  QFont monospace_font_;
  Tokens tokens_;
  QString diagnostic_;
  bool base_style_installed_ = false;
  bool style_hint_connected_ = false;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_APPLICATION_THEME_H
