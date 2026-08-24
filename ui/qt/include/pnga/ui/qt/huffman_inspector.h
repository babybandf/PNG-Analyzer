#ifndef PNGA_UI_QT_HUFFMAN_INSPECTOR_H
#define PNGA_UI_QT_HUFFMAN_INSPECTOR_H

// WP-505B / WP-5U12: presentation-only Huffman table inspector. A page-local
// table-kind selector filters the already-published bounded tables; a compact
// Build/Symbol/Bits/Canonical table plus a details area show the selected
// entry. Qt never reconstructs a table or reads Deflate bits.

#include <pnga/analysis-engine/huffman_inspector.h>
#include <pnga/ui/qt/compression_inspector_page.h>

#include <QString>

class QComboBox;
class QLabel;
class QTableWidget;

namespace pnga::ui::qt {

class HuffmanInspector final : public CompressionInspectorPage {
  Q_OBJECT
 public:
  static constexpr int kMaxVisibleRows = 4096;
  explicit HuffmanInspector(QWidget* parent = nullptr);

  void setView(const pnga::analysis_engine::HuffmanInspectorView& view);
  void setExternalStatus(const QString& text);
  void clear();
  const pnga::analysis_engine::HuffmanInspectorView& view() const noexcept {
    return view_;
  }

 private:
  pnga::deflate_trace::HuffmanTableKind currentKind() const noexcept;
  void rebuildTable();
  void updateDetails();

  pnga::analysis_engine::HuffmanInspectorView view_;
  QLabel* heading_ = nullptr;
  QComboBox* selector_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_HUFFMAN_INSPECTOR_H
