#ifndef PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H
#define PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H

// WP-505C: presentation-only Decode Trace widget.

#include <pnga/analysis-engine/decode_trace_inspector.h>

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;
class QTableWidget;

namespace pnga::ui::qt {

class DecodeTraceInspector final : public QWidget {
  Q_OBJECT
 public:
  static constexpr int kMaxVisibleRows = 4096;
  explicit DecodeTraceInspector(QWidget* parent = nullptr);

  void setView(const pnga::analysis_engine::DecodeTraceInspectorView& view);
  void setExternalStatus(const QString& text);
  void clear();
  const pnga::analysis_engine::DecodeTraceInspectorView& view() const noexcept {
    return view_;
  }

 signals:
  void showInHexRequested(quint64 output_begin, quint64 output_end);
  void showInDeflateRequested(quint64 bit_begin, quint64 bit_end);

 private slots:
  void showSelectedInHex();
  void showSelectedInDeflate();

 private:
  void updateButtons();

  pnga::analysis_engine::DecodeTraceInspectorView view_;
  QLabel* status_ = nullptr;
  QLabel* context_ = nullptr;
  QTableWidget* table_ = nullptr;
  QPushButton* hex_button_ = nullptr;
  QPushButton* deflate_button_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H
