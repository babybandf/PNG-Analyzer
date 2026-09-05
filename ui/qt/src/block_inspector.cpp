// WP-5U12C DEFLATE Blocks page implementation. Model/view presentation of
// the complete Fast Index with typed actions only; Current and Manual
// Selection are driven by model roles so they can coexist with the native
// row selection.

#include "pnga/ui/qt/block_inspector.h"

#include "pnga/ui/qt/block_inspector_model.h"
#include "pnga/ui/qt/compression_selection_store.h"

#include <pnga/trace-model/provenance.h>
#include <pnga/ui/qt/application_theme.h>

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QTableView>

#include <algorithm>
#include <limits>
#include <utility>

namespace pnga::ui::qt {

namespace {

constexpr int kShowScanlinesWidth = 600;
constexpr int kShowEventsWidth = 420;

// Row-selection table keyboard contract (flow-ui §14): Up/Down move the
// native row selection and Home/End jump to the first/last row.
class BlocksTableView final : public QTableView {
 public:
  using QTableView::QTableView;

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (model() != nullptr && selectionModel() != nullptr) {
      if (event->key() == Qt::Key_Home && model()->rowCount() > 0) {
        selectRowOnKey(0);
        event->accept();
        return;
      }
      if (event->key() == Qt::Key_End && model()->rowCount() > 0) {
        selectRowOnKey(model()->rowCount() - 1);
        event->accept();
        return;
      }
    }
    QTableView::keyPressEvent(event);
  }

 private:
  void selectRowOnKey(int row) {
    selectionModel()->setCurrentIndex(
        model()->index(row, 0),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  }
};

QString span_text(
    const std::vector<pnga::trace_model::ProvenanceSpan>& spans) {
  if (spans.empty()) {
    return QStringLiteral("—");
  }
  QString out;
  for (const auto& span : spans) {
    if (!out.isEmpty()) {
      out += QStringLiteral(", ");
    }
    if (span.space != pnga::trace_model::ProvenanceSpace::kPhysicalFile ||
        span.length > std::numeric_limits<std::uint64_t>::max() -
                          span.offset) {
      out += QStringLiteral("file[overflow)");
      continue;
    }
    out += QStringLiteral("file[%1..%2)")
               .arg(static_cast<qulonglong>(span.offset))
               .arg(static_cast<qulonglong>(span.offset + span.length));
  }
  return out;
}

QString range_text(std::uint64_t begin, std::uint64_t end) {
  return QStringLiteral("[%1, %2)")
      .arg(static_cast<qulonglong>(begin))
      .arg(static_cast<qulonglong>(end));
}

}  // namespace

BlockInspector::BlockInspector(QWidget* parent)
    : CompressionInspectorPage(parent) {
  serial_base_ = static_cast<std::uint64_t>(
                     reinterpret_cast<std::uintptr_t>(this))
                 << 16;
  model_ = new BlockInspectorModel(this);
  table_ = new BlocksTableView(this);
  table_->setObjectName(QStringLiteral("compressionBlocksTable"));
  table_->setAccessibleName(QStringLiteral("DEFLATE blocks"));
  table_->setModel(model_);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  // Normative geometry (flow-ui §20.3): 28 px rows and header.
  table_->verticalHeader()->setDefaultSectionSize(28);
  table_->verticalHeader()->setVisible(false);
  table_->setMinimumHeight(80);
  // Never let table content drive the page/dock minimum width; narrow pages
  // scroll horizontally inside the viewport (WP-5U12 responsive contract).
  table_->setMinimumWidth(0);
  table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  table_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  auto* header = table_->horizontalHeader();
  header->setStretchLastSection(false);
  header->setFixedHeight(28);
  header->setSectionResizeMode(BlockInspectorModel::Current,
                               QHeaderView::Fixed);
  table_->setColumnWidth(BlockInspectorModel::Current, 28);
  // Fit content when opening a document, then leave every content column
  // draggable (including the last visible one). QTableView's native header
  // double-click refits a single column without changing its Interactive mode.
  header->setSectionResizeMode(BlockInspectorModel::Number,
                               QHeaderView::Interactive);
  header->setSectionResizeMode(BlockInspectorModel::Type,
                               QHeaderView::Interactive);
  header->setSectionResizeMode(BlockInspectorModel::Final,
                               QHeaderView::Interactive);
  header->setSectionResizeMode(BlockInspectorModel::InputBits,
                               QHeaderView::Interactive);
  header->setSectionResizeMode(BlockInspectorModel::OutputBytes,
                               QHeaderView::Interactive);
  header->setSectionResizeMode(BlockInspectorModel::Events,
                               QHeaderView::Interactive);
  header->setSectionResizeMode(BlockInspectorModel::Scanlines,
                               QHeaderView::Interactive);
  table_->resizeColumnsToContents();
  table_->setColumnWidth(BlockInspectorModel::Current, 28);

  // The shell builds a provisional QTableWidget; the product page replaces
  // it with the model-backed view. The shell itself is outside this work
  // package, so the placeholder is removed through the shared splitter.
  if (auto* splitter =
          findChild<QSplitter*>(QStringLiteral("compressionPageSplitter"));
      splitter != nullptr) {
    QWidget* replaced = splitter->replaceWidget(0, table_);
    delete replaced;
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 55);
    splitter->setStretchFactor(1, 45);
  }

  hex_button_ = new QPushButton(QStringLiteral("Show in Hex"), this);
  hex_button_->setObjectName(QStringLiteral("blockShowInHex"));
  inflated_button_ =
      new QPushButton(QStringLiteral("Show inflated output"), this);
  inflated_button_->setObjectName(QStringLiteral("blockShowInflatedOutput"));
  hex_button_->setEnabled(false);
  inflated_button_->setEnabled(false);
  auto* buttons = new QHBoxLayout;
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(hex_button_);
  buttons->addWidget(inflated_button_);
  buttons->addStretch(1);
  layout()->addItem(buttons);

  // The drill-in action lives in the details area (flow-ui §7.2): it opens
  // the bounded trace window for the selected block only.
  open_trace_button_ =
      new QPushButton(QStringLiteral("Open Decode Trace"), this);
  open_trace_button_->setObjectName(QStringLiteral("blockOpenDecodeTrace"));
  open_trace_button_->setEnabled(false);
  if (auto* details_host =
          findChild<QWidget*>(QStringLiteral("compressionDetails"));
      details_host != nullptr && details_host->layout() != nullptr) {
    details_host->layout()->addWidget(open_trace_button_);
  }

  connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &BlockInspector::onSelectionChanged);
  connect(hex_button_, &QPushButton::clicked, this,
          &BlockInspector::showSelectedInHex);
  connect(inflated_button_, &QPushButton::clicked, this,
          &BlockInspector::showSelectedInflatedOutput);
  connect(open_trace_button_, &QPushButton::clicked, this,
          &BlockInspector::openDecodeTrace);
  updateResponsiveColumns();
}

void BlockInspector::setView(
    const pnga::analysis_engine::BlockInspectorView& view) {
  view_ = view;
  updateDetails();
  updateButtons();
  if (isVisible()) {
    scrollToCurrentRow();
  }
}

void BlockInspector::setFastIndex(
    const pnga::analysis_engine::FastCompressionIndexView& view) {
  // Per-document refit policy: the content-derived initial widths are
  // re-derived only when the published generation changes (document open);
  // a same-generation republish (row publish, Current change, selection
  // change) preserves the user's manual column widths.
  const bool generation_changed =
      !has_fast_index_ || view.generation != fast_index_.generation;
  fast_index_ = view;
  has_fast_index_ = true;
  model_->setIndex(
      std::make_shared<const pnga::analysis_engine::FastCompressionIndexView>(
          view));
  updateDetails();
  updateButtons();
  updateResponsiveColumns();
  if (generation_changed) {
    table_->resizeColumnsToContents();
    table_->setColumnWidth(BlockInspectorModel::Current, 28);
  }
  if (isVisible()) {
    scrollToCurrentRow();
  }
}

void BlockInspector::clearFastIndex() {
  fast_index_ = pnga::analysis_engine::FastCompressionIndexView{};
  has_fast_index_ = false;
  model_->setIndex(nullptr);
  updateDetails();
  updateButtons();
}

void BlockInspector::clear() {
  view_ = pnga::analysis_engine::BlockInspectorView{};
  clearFastIndex();
}

void BlockInspector::setExternalStatus(const QString& /*text*/) {
  // WP-5U12: the shared Compression context owns the trace status.
}

void BlockInspector::setSelectionStore(CompressionSelectionStore* store) {
  if (selection_store_ == store) {
    return;
  }
  if (store_connection_) {
    disconnect(store_connection_);
    store_connection_ = QMetaObject::Connection{};
  }
  selection_store_ = store;
  if (selection_store_ != nullptr) {
    store_connection_ = connect(
        selection_store_,
        &CompressionSelectionStore::stateChanged, this,
        [this](const pnga::trace_model::CompressionSelectionState& state) {
          model_->setSelectionState(state);
        });
    model_->setSelectionState(selection_store_->state());
  }
}

void BlockInspector::setSelectionState(
    const pnga::trace_model::CompressionSelectionState& state) {
  model_->setSelectionState(state);
}

std::optional<int> BlockInspector::activeRow() const noexcept {
  const auto rows = table_->selectionModel()->selectedRows();
  if (rows.size() == 1 && model_->rowAt(rows.front().row()) != nullptr) {
    return rows.front().row();
  }
  // Without a manual selection the details and actions follow the current
  // block (the block that produced the selected output byte).
  if (has_fast_index_ && view_.selected_block_index.has_value()) {
    for (int row = 0; row < static_cast<int>(fast_index_.blocks.size());
         ++row) {
      if (fast_index_.blocks[row].block_index ==
          *view_.selected_block_index) {
        return row;
      }
    }
  }
  return std::nullopt;
}

std::uint64_t BlockInspector::nextRequestSerial() const noexcept {
  // Page-local monotonic serial. The page pointer forms the high bits so
  // several pages can emit into one store without serial collisions.
  return serial_base_ + (++serial_counter_);
}

std::optional<pnga::trace_model::CompressionNavigationTarget>
BlockInspector::blockTargetFor(int row) const noexcept {
  const auto* block = model_->rowAt(row);
  if (block == nullptr || !has_fast_index_) {
    return std::nullopt;
  }
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = fast_index_.generation;
  target.request_serial = nextRequestSerial();
  target.source_unit = pnga::trace_model::DocumentSourceUnit{};
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kBlocks;
  target.logical_range = block->input_range;
  // Every physical span is validated and converted in original order; the
  // stored ProvenanceSpan values are never replaced by byte envelopes.
  target.physical_spans.reserve(block->physical_spans.size());
  for (const auto& span : block->physical_spans) {
    if (span.space != pnga::trace_model::ProvenanceSpace::kPhysicalFile ||
        span.length == 0) {
      return std::nullopt;
    }
    const auto range = pnga::trace_model::make_range(
        pnga::trace_model::FileByteOffset{span.offset}, span.length);
    if (!range.has_value()) {
      return std::nullopt;
    }
    target.physical_spans.push_back(*range);
  }
  if (target.physical_spans.empty()) {
    return std::nullopt;
  }
  target.block_index = block->block_index;
  return target;
}

void BlockInspector::onSelectionChanged() {
  const auto rows = table_->selectionModel()->selectedRows();
  if (rows.size() == 1) {
    // Row selection changes only Manual Selection: an in-place store update
    // without a history entry, without a navigation request and without any
    // trace submission. Without a store the typed target is emitted.
    if (auto target = blockTargetFor(rows.front().row());
        target.has_value() && target->valid()) {
      if (selection_store_ != nullptr) {
        selection_store_->setManual(*target);
      } else {
        emit navigationRequested(*target);
      }
    }
  }
  updateButtons();
  updateDetails();
}

void BlockInspector::updateButtons() {
  const std::optional<int> row = activeRow();
  const auto* block = row.has_value() ? model_->rowAt(*row) : nullptr;
  hex_button_->setEnabled(block != nullptr &&
                          !block->physical_spans.empty());
  inflated_button_->setEnabled(
      block != nullptr && block->output_range.valid() &&
      !block->output_range.empty());
  open_trace_button_->setEnabled(block != nullptr);
}

void BlockInspector::showSelectedInHex() {
  const std::optional<int> row = activeRow();
  if (!row.has_value()) {
    return;
  }
  auto target = blockTargetFor(*row);
  if (!target.has_value()) {
    return;
  }
  if (selection_store_ != nullptr) {
    selection_store_->applyNavigation(*target);
  } else {
    emit navigationRequested(*target);
  }
}

void BlockInspector::showSelectedInflatedOutput() {
  const std::optional<int> row = activeRow();
  if (!row.has_value()) {
    return;
  }
  const auto* block = model_->rowAt(*row);
  if (block == nullptr || !has_fast_index_ || !block->output_range.valid() ||
      block->output_range.empty()) {
    return;
  }
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = fast_index_.generation;
  target.request_serial = nextRequestSerial();
  target.source_unit = pnga::trace_model::DocumentSourceUnit{};
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kBlocks;
  target.logical_range = block->output_range;
  target.block_index = block->block_index;
  if (selection_store_ != nullptr) {
    selection_store_->applyNavigation(target);
  } else {
    emit navigationRequested(target);
  }
}

void BlockInspector::openDecodeTrace() {
  const std::optional<int> row = activeRow();
  if (!row.has_value()) {
    return;
  }
  const auto* block = model_->rowAt(*row);
  if (block == nullptr || !has_fast_index_) {
    return;
  }
  emit decodeTraceRequested(fast_index_.generation, block->block_index,
                            block->output_range);
}

void BlockInspector::showEvent(QShowEvent* event) {
  CompressionInspectorPage::showEvent(event);
  // One-time normative 55:45 split; later user adjustments are never reset.
  if (!splitter_sized_) {
    auto* splitter =
        findChild<QSplitter*>(QStringLiteral("compressionPageSplitter"));
    if (splitter != nullptr && splitter->height() > 0) {
      const int total = splitter->height();
      const int top = total * 55 / 100;
      splitter->setSizes({top, total - top});
      splitter_sized_ = true;
    }
  }
  updateResponsiveColumns();
  scrollToCurrentRow();
}

void BlockInspector::resizeEvent(QResizeEvent* event) {
  CompressionInspectorPage::resizeEvent(event);
  updateResponsiveColumns();
}

void BlockInspector::updateResponsiveColumns() {
  const int page_width = width();
  table_->setColumnHidden(BlockInspectorModel::Scanlines,
                          page_width < kShowScanlinesWidth);
  table_->setColumnHidden(BlockInspectorModel::Events,
                          page_width < kShowEventsWidth);
}

void BlockInspector::scrollToCurrentRow() {
  if (!has_fast_index_) {
    return;
  }
  for (std::size_t i = 0; i < fast_index_.blocks.size(); ++i) {
    if (fast_index_.blocks[i].block_index ==
        view_.selected_block_index.value_or(0)) {
      if (!view_.selected_block_index.has_value()) {
        return;
      }
      table_->scrollTo(model_->index(static_cast<int>(i), 0),
                       QAbstractItemView::PositionAtCenter);
      return;
    }
  }
}

void BlockInspector::updateDetails() {
  const std::optional<int> row = activeRow();
  const auto* block = row.has_value() ? model_->rowAt(*row) : nullptr;
  if (block == nullptr) {
    const pnga::analysis_engine::FastCompressionIndexStatus fast_status =
        has_fast_index_ ? fast_index_.status
                        : pnga::analysis_engine::
                              FastCompressionIndexStatus::kUnavailable;
    if (has_fast_index_ && !fast_index_.blocks.empty()) {
      setDetailsInstruction(
          QStringLiteral("Select a block to inspect its provenance, or lock "
                         "a pixel to find the current block."));
    } else if (fast_status ==
               pnga::analysis_engine::FastCompressionIndexStatus::kError) {
      setDetailsInstruction(QStringLiteral("Trace stopped: %1")
                                .arg(QString::fromStdString(
                                    fast_index_.error)));
    } else if (fast_status ==
               pnga::analysis_engine::FastCompressionIndexStatus::kPartial) {
      setDetailsInstruction(QStringLiteral(
          "Partial trace · verified blocks are shown above."));
    } else {
      setDetailsInstruction(QStringLiteral(
          "The DEFLATE block list appears once the zlib stream is "
          "indexed."));
    }
    return;
  }

  std::vector<std::pair<QString, QString>> details;
  details.emplace_back(
      QStringLiteral("Type"),
      QString::fromLatin1(pnga::deflate_index::block_type_text(block->type)));
  details.emplace_back(
      QStringLiteral("Final"),
      block->last ? QStringLiteral("yes · BFINAL=1")
                  : QStringLiteral("no · BFINAL=0"));
  details.emplace_back(
      QStringLiteral("Input"),
      QStringLiteral("zlib bits %1")
          .arg(range_text(block->input_range.begin.value,
                          block->input_range.end.value)));
  details.emplace_back(
      QStringLiteral("Output"),
      QStringLiteral("Inflated bytes %1")
          .arg(range_text(block->output_range.begin.value,
                          block->output_range.end.value)));
  details.emplace_back(
      QStringLiteral("Stored length"),
      block->stored_length.has_value()
          ? QStringLiteral("%1 bytes")
                .arg(static_cast<qulonglong>(*block->stored_length))
          : QStringLiteral("—"));
  details.emplace_back(
      QStringLiteral("Events"),
      block->event_count.has_value()
          ? QString::number(
                static_cast<qulonglong>(*block->event_count))
          : QStringLiteral("—"));
  details.emplace_back(
      QStringLiteral("Scanlines"),
      (block->first_scanline.has_value() && block->last_scanline.has_value())
          ? QStringLiteral("%1–%2")
                .arg(static_cast<qulonglong>(*block->first_scanline))
                .arg(static_cast<qulonglong>(*block->last_scanline))
          : QStringLiteral("—"));
  const auto bounded_row = std::find_if(
      view_.rows.begin(), view_.rows.end(), [&](const auto& candidate) {
        return candidate.block_index == block->block_index;
      });
  details.emplace_back(
      QStringLiteral("Current"),
      bounded_row != view_.rows.end() &&
              bounded_row->current_output_position.has_value()
          ? QStringLiteral("output byte %1")
                .arg(static_cast<qulonglong>(
                    *bounded_row->current_output_position))
          : QStringLiteral("—"));
  details.emplace_back(QStringLiteral("IDAT spans"),
                       span_text(block->physical_spans));
  details.emplace_back(QStringLiteral("Cross-IDAT"),
                       block->physical_spans.size() > 1
                           ? QStringLiteral("yes")
                           : QStringLiteral("no"));
  if (has_fast_index_) {
    const auto& wrapper = fast_index_.stream.wrapper;
    QString wrapper_state;
    if (wrapper.preset_dictionary) {
      wrapper_state = QStringLiteral("FDICT present");
    } else if (wrapper.header_valid) {
      wrapper_state = QStringLiteral("header valid");
    } else {
      wrapper_state = QStringLiteral("header invalid");
    }
    details.emplace_back(
        QStringLiteral("Wrapper"),
        QStringLiteral("CM %1 · window %2 bits · %3")
            .arg(wrapper.compression_method)
            .arg(wrapper.window_bits)
            .arg(wrapper_state));
    if (fast_index_.stream.stop_input.has_value() ||
        fast_index_.stream.stop_output.has_value()) {
      QString stop;
      if (fast_index_.stream.stop_input.has_value()) {
        stop += QStringLiteral("zlib bit %1")
                    .arg(static_cast<qulonglong>(
                        fast_index_.stream.stop_input->value));
      }
      if (fast_index_.stream.stop_output.has_value()) {
        if (!stop.isEmpty()) {
          stop += QStringLiteral(" · ");
        }
        stop += QStringLiteral("output byte %1")
                    .arg(static_cast<qulonglong>(
                        fast_index_.stream.stop_output->value));
      }
      details.emplace_back(QStringLiteral("Stopped at"), stop);
    }
    if (!fast_index_.error.empty()) {
      details.emplace_back(
          QStringLiteral("Error"),
          QString::fromStdString(fast_index_.error));
    }
  }
  setDetails(QStringLiteral("Block #%1 details")
                 .arg(static_cast<qulonglong>(block->block_index)),
             details);
}

}  // namespace pnga::ui::qt
