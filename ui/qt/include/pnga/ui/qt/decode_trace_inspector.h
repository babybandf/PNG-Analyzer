#ifndef PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H
#define PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H

// WP-5U12E: the product Decode Trace page. A DecodeTraceModel backed
// QTableView renders the bounded semantic steps (no QTableWidget, no per-row
// widgets); the page owns the scope heading, the structured details, the
// Current/Manual Selection coexistence and the two distinct typed actions:
// Show in Hex carries the compressed DeflateBitRange with every physical
// file span, Show inflated output carries only the InflatedByteRange. The
// page never decodes and never parses event text.

#include <pnga/analysis-engine/decode_trace_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/compression_inspector_page.h>
#include <pnga/ui/qt/compression_selection_store.h>
#include <pnga/ui/qt/decode_trace_model.h>

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

class QLabel;
class QPushButton;
class QShowEvent;
class QTableView;

namespace pnga::ui::qt {

class DecodeTraceModel;

class DecodeTraceInspector final : public CompressionInspectorPage {
  Q_OBJECT
 public:
  // Kept for the WP-5U15 performance gate; the model-backed table itself
  // always shows the complete published step list (WP-5U12E).
  static constexpr int kMaxVisibleRows = 4096;
  explicit DecodeTraceInspector(QWidget* parent = nullptr);

  // Publishes the bounded Decode Trace projection.
  void setView(const pnga::analysis_engine::DecodeTraceInspectorView& view);
  void setExternalStatus(const QString& text);
  void clear();
  const pnga::analysis_engine::DecodeTraceInspectorView& view() const noexcept {
    return view_;
  }

  // Attaches the shared WP-5U12B store. Row selection publishes a Manual
  // target through the store (no history entry, no navigation, zero trace
  // submission); the two Show actions request typed B navigation through it.
  // Without a store the page emits the typed targets instead. The store must
  // outlive the page.
  void setSelectionStore(CompressionSelectionStore* store);
  void setSelectionState(
      const pnga::trace_model::CompressionSelectionState& state);

 signals:
  // The only signal of this page: one typed navigation target per action.
  void navigationRequested(
      const pnga::trace_model::CompressionNavigationTarget& target);

 private slots:
  void onSelectionChanged();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  std::optional<int> activeRow() const noexcept;
  std::optional<pnga::trace_model::CompressionNavigationTarget>
  manualTargetFor(const pnga::analysis_engine::DecodeTraceStep& step) const
      noexcept;
  std::optional<pnga::trace_model::CompressionNavigationTarget>
  hexTargetFor(const pnga::analysis_engine::DecodeTraceStep& step) const
      noexcept;
  std::optional<pnga::trace_model::CompressionNavigationTarget>
  inflatedTargetFor(const pnga::analysis_engine::DecodeTraceStep& step) const
      noexcept;
  std::uint64_t nextRequestSerial() const noexcept;
  void updateScopeHeading();
  void updateButtons();
  void updateDetails();

  pnga::analysis_engine::DecodeTraceInspectorView view_;
  DecodeTraceModel* model_ = nullptr;
  QTableView* table_ = nullptr;
  CompressionSelectionStore* selection_store_ = nullptr;
  QMetaObject::Connection store_connection_;
  QLabel* scope_heading_ = nullptr;
  QPushButton* hex_button_ = nullptr;
  QPushButton* inflated_button_ = nullptr;
  bool applying_state_ = false;
  bool splitter_sized_ = false;
  mutable std::uint64_t serial_base_ = 0;
  mutable std::uint64_t serial_counter_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_DECODE_TRACE_INSPECTOR_H
