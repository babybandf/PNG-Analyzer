#ifndef PNGA_UI_QT_BLOCK_INSPECTOR_H
#define PNGA_UI_QT_BLOCK_INSPECTOR_H

// WP-5U12C: the product DEFLATE Blocks page. A BlockInspectorModel backed
// QTableView exposes the complete generation-level Fast Index without any
// pixel lock; details, actions and Current/Manual Selection rendering live
// here while the typed facts come from the immutable projection. The page
// never parses PNG/DEFLATE data and never replays or guesses geometry.

#include <pnga/analysis-engine/block_inspector.h>
#include <pnga/trace-model/compression_navigation.h>
#include <pnga/ui/qt/block_inspector_model.h>
#include <pnga/ui/qt/compression_inspector_page.h>
#include <pnga/ui/qt/compression_selection_store.h>

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>

class QPushButton;
class QShowEvent;
class QTableView;

namespace pnga::ui::qt {

class BlockInspectorModel;

class BlockInspector final : public CompressionInspectorPage {
  Q_OBJECT
 public:
  // Kept for the WP-5U15 performance gate; the model-backed table itself
  // always shows the complete row count (WP-5U12C).
  static constexpr int kMaxVisibleRows = 2048;
  explicit BlockInspector(QWidget* parent = nullptr);

  // Bounded trace bundle publication: contributes status copy and current
  // byte facts for the details area. Table rows come from the Fast Index.
  void setView(const pnga::analysis_engine::BlockInspectorView& view);
  void setFastIndex(
      const pnga::analysis_engine::FastCompressionIndexView& view);
  void clearFastIndex();
  void setExternalStatus(const QString& text);
  void clear();
  const pnga::analysis_engine::BlockInspectorView& view() const noexcept {
    return view_;
  }

  // Attaches the shared WP-5U12B store. Row selection publishes a Manual
  // target through the store (no history entry, no navigation); the Show
  // actions request B navigation through it. Without a store the page emits
  // the typed targets instead. The store must outlive the page.
  void setSelectionStore(CompressionSelectionStore* store);
  void setSelectionState(
      const pnga::trace_model::CompressionSelectionState& state);

 signals:
  void navigationRequested(
      const pnga::trace_model::CompressionNavigationTarget& target);
  void decodeTraceRequested(std::uint64_t generation,
                            std::uint64_t block_index,
                            pnga::trace_model::InflatedByteRange output_range);

 private slots:
  void onSelectionChanged();
  void showSelectedInHex();
  void showSelectedInflatedOutput();
  void openDecodeTrace();

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  std::optional<int> activeRow() const noexcept;
  std::optional<pnga::trace_model::CompressionNavigationTarget>
  blockTargetFor(int row) const noexcept;
  std::uint64_t nextRequestSerial() const noexcept;
  void updateButtons();
  void updateDetails();
  void updateResponsiveColumns();
  void scrollToCurrentRow();

  pnga::analysis_engine::BlockInspectorView view_;
  pnga::analysis_engine::FastCompressionIndexView fast_index_;
  bool has_fast_index_ = false;
  BlockInspectorModel* model_ = nullptr;
  QTableView* table_ = nullptr;
  CompressionSelectionStore* selection_store_ = nullptr;
  QMetaObject::Connection store_connection_;
  QPushButton* hex_button_ = nullptr;
  QPushButton* inflated_button_ = nullptr;
  QPushButton* open_trace_button_ = nullptr;
  bool splitter_sized_ = false;
  mutable std::uint64_t serial_base_ = 0;
  mutable std::uint64_t serial_counter_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_BLOCK_INSPECTOR_H
