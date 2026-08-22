#ifndef PNGA_UI_QT_HUFFMAN_INSPECTOR_H
#define PNGA_UI_QT_HUFFMAN_INSPECTOR_H

// WP-505B: presentation-only Huffman table inspector.

#include <pnga/analysis-engine/huffman_inspector.h>

#include <QWidget>
#include <QString>

class QLabel;
class QTableWidget;

namespace pnga::ui::qt {

class HuffmanInspector final : public QWidget {
  Q_OBJECT
 public:
  explicit HuffmanInspector(QWidget* parent = nullptr);

  void setView(const pnga::analysis_engine::HuffmanInspectorView& view);
  void setExternalStatus(const QString& text);
  void clear();
  const pnga::analysis_engine::HuffmanInspectorView& view() const noexcept {
    return view_;
  }

 private:
  pnga::analysis_engine::HuffmanInspectorView view_;
  QLabel* status_ = nullptr;
  QLabel* context_ = nullptr;
  QTableWidget* table_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_HUFFMAN_INSPECTOR_H
