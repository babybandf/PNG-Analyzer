#ifndef PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H
#define PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H

// WP-505C / WP-5U12: presentation-only Decode Trace widget. A compact
// Current/Token/Path/Input/Output table plus a details area explain Literal,
// Match and EOB tokens from immutable projection rows; the GUI never decodes.

#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/ui/qt/compression_inspector_page.h>

#include <QString>

class QPushButton;
class QTableWidget;

namespace pnga::ui::qt {

class DecodeTraceInspector final : public CompressionInspectorPage {
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
  void onSelectionChanged();
  void showSelectedInHex();
  void showSelectedInDeflate();

 private:
  std::optional<std::size_t> activeStep() const noexcept;
  void updateButtons();
  void updateDetails();

  pnga::analysis_engine::DecodeTraceInspectorView view_;
  QPushButton* hex_button_ = nullptr;
  QPushButton* deflate_button_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H
