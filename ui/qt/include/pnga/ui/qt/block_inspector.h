#ifndef PNGA_UI_QT_BLOCK_INSPECTOR_H
#define PNGA_UI_QT_BLOCK_INSPECTOR_H

// WP-505A / WP-5U12: the Qt presentation for the Qt-free BlockInspectorView.
// Master/detail layout: a compact Current/#/Type/Final/Input/Output table plus
// a details area for the selected/current block. This widget formats immutable
// rows only; it does not inspect PNG or Deflate data.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/ui/qt/compression_inspector_page.h>

#include <QString>

class QPushButton;
class QTableWidget;

namespace pnga::ui::qt {

class BlockInspector final : public CompressionInspectorPage {
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
  void onSelectionChanged();
  void showSelectedInHex();
  void showSelectedInDeflate();

 private:
  std::optional<std::size_t> activeRow() const noexcept;
  void updateButtons();
  void updateDetails();

  pnga::analysis_engine::BlockInspectorView view_;
  QPushButton* hex_button_ = nullptr;
  QPushButton* deflate_button_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_BLOCK_INSPECTOR_H
