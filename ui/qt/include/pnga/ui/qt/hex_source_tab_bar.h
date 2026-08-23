#ifndef PNGA_UI_QT_HEX_SOURCE_TAB_BAR_H
#define PNGA_UI_QT_HEX_SOURCE_TAB_BAR_H

// WP-5U11: compact, strongly mapped selector for the shared HexView.

#include <pnga/ui/qt/selection_view_state.h>

#include <QTabBar>

namespace pnga::ui::qt {

class HexSourceTabBar final : public QTabBar {
  Q_OBJECT
 public:
  explicit HexSourceTabBar(QWidget* parent = nullptr);

  HexSource source() const noexcept;
  void setSource(HexSource source);

 signals:
  void sourceChanged(pnga::ui::qt::HexSource source);

 private slots:
  void onCurrentChanged(int index);

 private:
  static HexSource sourceForIndex(int index) noexcept;
  static int indexForSource(HexSource source) noexcept;
};

}  // namespace pnga::ui::qt

Q_DECLARE_METATYPE(pnga::ui::qt::HexSource)

#endif  // PNGA_UI_QT_HEX_SOURCE_TAB_BAR_H
