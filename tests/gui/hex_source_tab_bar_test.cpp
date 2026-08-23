// WP-5U11: the shared HexView source selector keeps stable item-data mapping.

#include <pnga/ui/qt/hex_source_tab_bar.h>

#include <QtTest/QtTest>

#include <QSignalSpy>

class HexSourceTabBarTest : public QObject {
  Q_OBJECT
 private slots:
  void exposesStableSourcesAndMetadata();
  void selectionEmitsStronglyTypedSource();
  void programmaticSelectionDoesNotEmitDuplicateSignal();
};

void HexSourceTabBarTest::exposesStableSourcesAndMetadata() {
  pnga::ui::qt::HexSourceTabBar bar;
  QCOMPARE(bar.count(), 4);
  QCOMPARE(bar.shape(), QTabBar::RoundedWest);
  QVERIFY(bar.styleSheet().contains(QStringLiteral("padding-left: 8px")));
  QVERIFY(bar.styleSheet().contains(QStringLiteral("padding-right: 8px")));
  QVERIFY(bar.styleSheet().contains(QStringLiteral("background-color: #d0d0d0")));
  QVERIFY(bar.styleSheet().contains(QStringLiteral("font-weight: bold")));
  QCOMPARE(bar.tabText(0), QStringLiteral("File"));
  QCOMPARE(bar.tabText(1), QStringLiteral("IDAT"));
  QCOMPARE(bar.tabText(2), QStringLiteral("Inflated"));
  QCOMPARE(bar.tabText(3), QStringLiteral("Defiltered"));
  for (int index = 0; index < bar.count(); ++index) {
    QCOMPARE(bar.tabData(index).toInt(), index);
    QVERIFY(!bar.tabToolTip(index).isEmpty());
    QVERIFY(!bar.tabWhatsThis(index).isEmpty());
  }
  QCOMPARE(bar.source(), pnga::ui::qt::HexSource::kFile);
}

void HexSourceTabBarTest::selectionEmitsStronglyTypedSource() {
  pnga::ui::qt::HexSourceTabBar bar;
  QSignalSpy spy(&bar, &pnga::ui::qt::HexSourceTabBar::sourceChanged);
  bar.setCurrentIndex(2);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).value<pnga::ui::qt::HexSource>(),
           pnga::ui::qt::HexSource::kInflated);
  QCOMPARE(bar.source(), pnga::ui::qt::HexSource::kInflated);
}

void HexSourceTabBarTest::programmaticSelectionDoesNotEmitDuplicateSignal() {
  pnga::ui::qt::HexSourceTabBar bar;
  QSignalSpy spy(&bar, &pnga::ui::qt::HexSourceTabBar::sourceChanged);
  bar.setSource(pnga::ui::qt::HexSource::kDefiltered);
  QCOMPARE(spy.count(), 0);
  QCOMPARE(bar.currentIndex(), 3);
  QCOMPARE(bar.source(), pnga::ui::qt::HexSource::kDefiltered);
}

QTEST_MAIN(HexSourceTabBarTest)
#include "hex_source_tab_bar_test.moc"
