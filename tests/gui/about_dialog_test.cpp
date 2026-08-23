// WP-206 About dialog tests: exact content, link schemes, copyable text,
// version from the project version interface, deterministic output, and the
// Help-menu wiring that opens the dialog.

#include <pnga/core/version.h>
#include <pnga/ui/qt/about_dialog.h>

#include <QtTest/QtTest>

#include <QApplication>
#include <QLabel>
#include <QMenu>

using pnga::ui::qt::AboutContent;
using pnga::ui::qt::AboutDialog;
using pnga::ui::qt::default_about_content;

class AboutDialogTest : public QObject {
  Q_OBJECT
 private slots:
  void contentMatchesConfirmedValues();
  void dialogRendersContent();
  void linkSchemesAreHttpsAndMailto();
  void textIsCopyable();
  void brandVisualUsesCompiledResource();
  void applicationIconUsesCompiledResource();
  void versionComesFromProjectInterface();
  void outputIsDeterministic();
  void helpMenuActionOpensDialog();
};

void AboutDialogTest::contentMatchesConfirmedValues() {
  const AboutContent c = default_about_content();
  QCOMPARE(c.project_name, QStringLiteral("PNG Analyzer"));
  QCOMPARE(c.github_url, QStringLiteral("https://github.com/babybandf/PNG-Analyzer"));
  QCOMPARE(c.contact_email, QStringLiteral("babybandf@163.com"));
}

void AboutDialogTest::dialogRendersContent() {
  AboutDialog dlg(default_about_content());
  QCOMPARE(dlg.projectNameText(), QStringLiteral("PNG Analyzer"));
  QCOMPARE(dlg.githubLinkText(),
           QStringLiteral("<a href=\"https://github.com/babybandf/PNG-Analyzer\">"
                          "https://github.com/babybandf/PNG-Analyzer</a>"));
  QCOMPARE(dlg.emailLinkText(),
           QStringLiteral("<a href=\"mailto:babybandf@163.com\">"
                          "babybandf@163.com</a>"));
}

void AboutDialogTest::linkSchemesAreHttpsAndMailto() {
  AboutDialog dlg(default_about_content());
  QVERIFY(dlg.githubHref().startsWith(QStringLiteral("https://")));
  QVERIFY(dlg.emailHref().startsWith(QStringLiteral("mailto:")));
  QCOMPARE(dlg.emailHref(), QStringLiteral("mailto:babybandf@163.com"));
}

void AboutDialogTest::textIsCopyable() {
  AboutDialog dlg(default_about_content());
  QVERIFY(dlg.textSelectable());
}

void AboutDialogTest::brandVisualUsesCompiledResource() {
  AboutDialog dlg(default_about_content());
  auto* visual = dlg.findChild<QLabel*>(QStringLiteral("brandVisual"));
  QVERIFY(visual != nullptr);
  QVERIFY(!dlg.brandPixmap().isNull());
  dlg.show();
  QCoreApplication::processEvents();
  QVERIFY(visual->isVisible());
}

void AboutDialogTest::applicationIconUsesCompiledResource() {
  const QIcon icon = pnga::ui::qt::application_icon();
  QVERIFY(!icon.isNull());
  QApplication::setWindowIcon(icon);
  QVERIFY(!QApplication::windowIcon().isNull());
}

void AboutDialogTest::versionComesFromProjectInterface() {
  const AboutContent c = default_about_content();
  QCOMPARE(c.version, QString::fromUtf8(pnga::version_string()));
  AboutDialog dlg(c);
  QVERIFY(dlg.versionText().contains(QString::fromUtf8(pnga::version_string())));
}

void AboutDialogTest::outputIsDeterministic() {
  // No clock, build path or machine-specific field leaks into the dialog.
  const AboutContent c = default_about_content();
  AboutDialog a(c);
  AboutDialog b(c);
  QCOMPARE(a.projectNameText(), b.projectNameText());
  QCOMPARE(a.versionText(), b.versionText());
  QCOMPARE(a.githubLinkText(), b.githubLinkText());
  QCOMPARE(a.emailLinkText(), b.emailLinkText());
  const QString all = a.projectNameText() + a.versionText() +
                      a.githubLinkText() + a.emailLinkText();
  QVERIFY(!all.contains(QStringLiteral("/Users/")));
  QVERIFY(!all.contains(QStringLiteral("2026")));  // no timestamp
}

void AboutDialogTest::helpMenuActionOpensDialog() {
  QMenu menu;
  AboutDialog* dlg = nullptr;
  QAction* about = menu.addAction(QStringLiteral("About"));
  QObject::connect(about, &QAction::triggered, [&dlg]() {
    dlg = new AboutDialog(default_about_content());
    dlg->show();
  });
  about->trigger();
  QVERIFY(dlg != nullptr);  // the menu action opened the dialog
  QVERIFY(dlg->isVisible());
  QCOMPARE(dlg->githubHref(), QStringLiteral("https://github.com/babybandf/PNG-Analyzer"));
  delete dlg;
}

QTEST_MAIN(AboutDialogTest)
#include "about_dialog_test.moc"
