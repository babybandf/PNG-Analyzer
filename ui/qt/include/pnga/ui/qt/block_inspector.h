#ifndef PNGA_UI_QT_BLOCK_INSPECTOR_H
#define PNGA_UI_QT_BLOCK_INSPECTOR_H

// WP-505A: the Qt presentation for the Qt-free BlockInspectorView. This
// widget formats immutable rows only; it does not inspect PNG or Deflate data.

#include <pnga/analysis-engine/block_inspector.h>

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;
class QTableWidget;

namespace pnga::ui::qt {

class BlockInspector final : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kMaxVisibleRows = 2048;
  explicit BlockInspector(QWidget* parent = nullptr);

  void setView(const pnga::analysis_engine::BlockInspectorView& view);
  void setExternalStatus(const QString& text);
  void clear();
  const pnga::analysis_engine::BlockInspectorView& view() const noexcept {
    return view_;
  }

 signals:
  void showInHexRequested(quint64 file_offset, quint64 length);
  void showInDeflateRequested(quint64 bit_begin, quint64 bit_end);

 private slots:
  void showSelectedInHex();
  void showSelectedInDeflate();

 private:
  void updateButtons();

  pnga::analysis_engine::BlockInspectorView view_;
  QLabel* status_ = nullptr;
  QLabel* context_ = nullptr;
  QTableWidget* table_ = nullptr;
  QPushButton* hex_button_ = nullptr;
  QPushButton* deflate_button_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_BLOCK_INSPECTOR_H
