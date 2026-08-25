#ifndef PNGA_UI_QT_BLOCK_INSPECTOR_H
#define PNGA_UI_QT_BLOCK_INSPECTOR_H

// WP-505A / WP-5U12: the Qt presentation for the Qt-free BlockInspectorView.
// Master/detail layout: a compact #/Type/Final/Input/Output table plus
// a details area for the selected/current block. This widget formats immutable
// rows only; it does not inspect PNG or Deflate data.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/ui/qt/compression_inspector_page.h>

#include <QPair>
#include <QString>
#include <QVector>

#include <optional>

class QPushButton;
class QShowEvent;
class QTableWidget;

namespace pnga::ui::qt {

class BlockInspector final : public CompressionInspectorPage {
  Q_OBJECT
 public:
  static constexpr int kMaxVisibleRows = 2048;
  explicit BlockInspector(QWidget* parent = nullptr);

  void setView(const pnga::analysis_engine::BlockInspectorView& view);
  void setFastIndex(
      const pnga::analysis_engine::FastCompressionIndexView& view);
  void clearFastIndex();
  void setExternalStatus(const QString& text);
  void clear();
  const pnga::analysis_engine::BlockInspectorView& view() const noexcept {
    return view_;
  }

 signals:
  // The legacy single-span signal remains for source compatibility with
  // existing integrations. New consumers must use the complete segmented
  // range signal so a block crossing IDAT chunks is not truncated.
  void showInHexRequested(quint64 file_offset, quint64 length);
  void showInHexSpansRequested(QVector<QPair<quint64, quint64>> spans);
  void showInDeflateRequested(quint64 bit_begin, quint64 bit_end);

 private slots:
  void onSelectionChanged();
  void showSelectedInHex();
  void showSelectedInDeflate();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  std::optional<std::size_t> activeRow() const noexcept;
  void updateButtons();
  void updateDetails();
  void renderView();
  void scrollToAssociatedRow();

  pnga::analysis_engine::BlockInspectorView view_;
  pnga::analysis_engine::BlockInspectorView bounded_view_;
  pnga::analysis_engine::FastCompressionIndexView fast_index_;
  bool has_fast_index_ = false;
  std::optional<int> associated_table_row_;
  QPushButton* hex_button_ = nullptr;
  QPushButton* deflate_button_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_BLOCK_INSPECTOR_H
