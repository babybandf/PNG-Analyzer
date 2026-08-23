// WP-206 About dialog implementation. Static, deterministic content; links are
// selectable (copyable) and open via QDesktopServices when activated.

#include "pnga/ui/qt/about_dialog.h"

#include <pnga/core/version.h>

#include <QDesktopServices>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

static void initialize_brand_resources() {
  Q_INIT_RESOURCE(png_analyzer_branding);
}

namespace pnga::ui::qt {

AboutContent default_about_content() {
  AboutContent content;
  content.version = QString::fromUtf8(pnga::version_string());
  return content;
}

QIcon application_icon() {
  initialize_brand_resources();
  QIcon icon;
  for (const int size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) {
    icon.addFile(QStringLiteral(":/pnga/icons/png-analyzer-%1.png").arg(size),
                 QSize(size, size));
  }
  return icon;
}

namespace {

// Rich-text link label that keeps its text selectable and routes activation
// through linkActivated (rather than auto-opening) so the dialog owns the
// QDesktopServices call.
QLabel* make_link_label(const QString& href, const QString& visible) {
  auto* label = new QLabel;
  label->setOpenExternalLinks(false);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                 Qt::LinksAccessibleByMouse);
  label->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(href, visible));
  return label;
}

}  // namespace

AboutDialog::AboutDialog(const AboutContent& content, QWidget* parent)
    : QDialog(parent), content_(content) {
  initialize_brand_resources();
  setWindowTitle(QStringLiteral("About PNG Analyzer"));

  brand_label_ = new QLabel(this);
  brand_label_->setObjectName(QStringLiteral("brandVisual"));
  brand_label_->setAccessibleName(QStringLiteral("PNG Analyzer brand"));
  brand_label_->setAlignment(Qt::AlignCenter);
  const QPixmap brand(
      QStringLiteral(":/pnga/branding/png-analyzer-lockup.png"));
  brand_label_->setPixmap(
      brand.scaled(QSize(128, 128), Qt::KeepAspectRatio,
                   Qt::SmoothTransformation));

  name_label_ = new QLabel(content_.project_name, this);
  QFont name_font = name_label_->font();
  name_font.setBold(true);
  name_font.setPointSize(name_font.pointSize() + 2);
  name_label_->setFont(name_font);
  name_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  version_label_ =
      new QLabel(QStringLiteral("Version %1").arg(content_.version), this);
  version_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  github_label_ = make_link_label(content_.github_url, content_.github_url);
  email_label_ =
      make_link_label(QStringLiteral("mailto:%1").arg(content_.contact_email),
                      content_.contact_email);

  const auto open_link = [](const QString& href) {
    QDesktopServices::openUrl(QUrl(href));  // system default app; text stays
  };                                        // selectable if this fails
  connect(github_label_, &QLabel::linkActivated, this, open_link);
  connect(email_label_, &QLabel::linkActivated, this, open_link);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(brand_label_);
  layout->addSpacing(4);
  layout->addWidget(name_label_);
  layout->addWidget(version_label_);
  layout->addSpacing(8);
  layout->addWidget(github_label_);
  layout->addWidget(email_label_);
}

QString AboutDialog::projectNameText() const {
  return name_label_ != nullptr ? name_label_->text() : QString();
}

QString AboutDialog::versionText() const {
  return version_label_ != nullptr ? version_label_->text() : QString();
}

QString AboutDialog::githubLinkText() const {
  return github_label_ != nullptr ? github_label_->text() : QString();
}

QString AboutDialog::emailLinkText() const {
  return email_label_ != nullptr ? email_label_->text() : QString();
}

QString AboutDialog::githubHref() const { return content_.github_url; }

QString AboutDialog::emailHref() const {
  return QStringLiteral("mailto:%1").arg(content_.contact_email);
}

bool AboutDialog::textSelectable() const {
  if (github_label_ == nullptr || email_label_ == nullptr ||
      version_label_ == nullptr) {
    return false;
  }
  const auto flags = github_label_->textInteractionFlags() &
                     email_label_->textInteractionFlags() &
                     version_label_->textInteractionFlags();
  return (flags & Qt::TextSelectableByMouse) != 0;
}

QPixmap AboutDialog::brandPixmap() const {
  return brand_label_ != nullptr ? brand_label_->pixmap() : QPixmap();
}

}  // namespace pnga::ui::qt
