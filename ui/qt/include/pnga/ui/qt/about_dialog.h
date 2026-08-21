#ifndef PNGA_UI_QT_ABOUT_DIALOG_H
#define PNGA_UI_QT_ABOUT_DIALOG_H

// WP-206: About dialog — project name, version, GitHub URL and maintainer
// contact email, all copyable and openable via the system default application.
// No network requests are made: the content is static and deterministic, and
// link activation is delegated to QDesktopServices only. If opening an
// external application fails, the text remains selectable and copyable.

#include <QDialog>
#include <QString>

class QLabel;

namespace pnga::ui::qt {

// Central About content definition (WP-206). Deterministic: no clock, build
// path or machine-specific field, so tests can assert the exact text. The
// version comes from the project version interface (pnga::version_string()).
struct AboutContent {
  QString project_name = QStringLiteral("PNG Analyzer");
  QString github_url =
      QStringLiteral("https://github.com/babybandf/PNG-Analyzer");
  QString contact_email = QStringLiteral("babybandf@163.com");
  QString version;  // filled from the project version interface
};

// Returns the standard About content with the version filled in.
AboutContent default_about_content();

class AboutDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit AboutDialog(const AboutContent& content, QWidget* parent = nullptr);

  QString projectNameText() const;
  QString versionText() const;
  QString githubLinkText() const;  // visible URL text
  QString emailLinkText() const;   // visible email text
  QString githubHref() const;      // link target (https scheme)
  QString emailHref() const;       // link target (mailto scheme)
  bool textSelectable() const;     // labels are copyable

 private:
  AboutContent content_;
  QLabel* name_label_ = nullptr;
  QLabel* version_label_ = nullptr;
  QLabel* github_label_ = nullptr;
  QLabel* email_label_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_ABOUT_DIALOG_H
