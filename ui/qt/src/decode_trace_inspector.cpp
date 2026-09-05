// WP-5U12E Decode Trace page implementation. Model/view presentation of the
// bounded semantic steps with typed actions only: row selection changes
// Manual Selection, Show in Hex carries the compressed DeflateBitRange with
// every physical file span, Show inflated output carries only the
// InflatedByteRange, and nothing here replays, decodes, parses PNG/DEFLATE
// or constructs facts from event text.

#include "pnga/ui/qt/decode_trace_inspector.h"

#include "pnga/ui/qt/compression_selection_store.h"

#include <pnga/ui/qt/application_theme.h>

#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QSplitter>
#include <QTableView>
#include <QVariant>

#include <cstdint>
#include <limits>
#include <utility>

namespace pnga::ui::qt {

namespace {

// Row-selection table keyboard contract (flow-ui §14): Up/Down move the
// native row selection, Home/End jump to the first/last row and Page Up/Down
// move by page through the native QTableView behavior.
class DecodeTraceTableView final : public QTableView {
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

QString bits_label(const pnga::trace_model::DeflateBitRange& range) {
  if (!range.valid() || range.empty()) {
    return QStringLiteral("—");
  }
  return QStringLiteral("%1–%2")
      .arg(static_cast<qulonglong>(range.begin.value))
      .arg(static_cast<qulonglong>(range.end.value));
}

QString bytes_label(const pnga::trace_model::InflatedByteRange& range) {
  if (!range.valid() || range.empty()) {
    return QStringLiteral("—");
  }
  if (range.end.value - range.begin.value == 1) {
    return QStringLiteral("%1")
        .arg(static_cast<qulonglong>(range.begin.value));
  }
  return QStringLiteral("%1–%2")
      .arg(static_cast<qulonglong>(range.begin.value))
      .arg(static_cast<qulonglong>(range.end.value));
}

QString sources_label(
    const std::vector<pnga::deflate_trace::TokenOutputRange>& ranges) {
  if (ranges.empty()) {
    return QStringLiteral("—");
  }
  QString out;
  for (const auto& range : ranges) {
    if (!out.isEmpty()) {
      out += QStringLiteral(", ");
    }
    out += QStringLiteral("[%1, %2) token %3")
               .arg(static_cast<qulonglong>(range.begin))
               .arg(static_cast<qulonglong>(range.end))
               .arg(static_cast<qulonglong>(range.token_index));
  }
  return out;
}

QString symbol_label(const std::optional<std::uint16_t>& symbol) {
  if (!symbol.has_value()) {
    return QStringLiteral("—");
  }
  return QStringLiteral("symbol %1").arg(*symbol);
}

}  // namespace

DecodeTraceInspector::DecodeTraceInspector(QWidget* parent)
    : CompressionInspectorPage(parent) {
  serial_base_ = static_cast<std::uint64_t>(
                     reinterpret_cast<std::uintptr_t>(this))
                 << 16;
  model_ = new DecodeTraceModel(this);
  table_ = new DecodeTraceTableView(this);
  table_->setObjectName(QStringLiteral("compressionDecodeTraceTable"));
  table_->setAccessibleName(QStringLiteral("Decode trace events"));
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
  header->setFixedHeight(28);
  header->setStretchLastSection(false);
  // Defect 2026-09-05: the content columns stay user-adjustable
  // (Interactive) instead of ResizeToContents; the initial widths are
  // re-derived from content on every publish so a fresh open shows the
  // exact widths the ResizeToContents mode used to produce. The Event
  // column keeps the frozen normative Stretch mode (product gate and
  // responsive matrix).
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setSectionResizeMode(DecodeTraceModel::Event, QHeaderView::Stretch);
  table_->resizeColumnsToContents();

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

  // Scope heading: the title facts of the bounded result (output range,
  // returned token count, status, truncation, stop reason); it never implies
  // a whole-stream trace (flow-ui §9.2).
  scope_heading_ = new QLabel(this);
  scope_heading_->setObjectName(QStringLiteral("decodeTraceScopeHeading"));
  scope_heading_->setWordWrap(true);
  static_cast<QBoxLayout*>(layout())->insertWidget(0, scope_heading_);

  // Footer actions in the normative order and locked labels (flow-ui
  // §20.2/§20.7): compressed input first, inflated output second.
  hex_button_ = new QPushButton(QStringLiteral("Show in Hex"), this);
  hex_button_->setObjectName(QStringLiteral("decodeShowInHex"));
  inflated_button_ =
      new QPushButton(QStringLiteral("Show inflated output"), this);
  inflated_button_->setObjectName(QStringLiteral("decodeShowInflatedOutput"));
  hex_button_->setEnabled(false);
  inflated_button_->setEnabled(false);
  auto* buttons = new QHBoxLayout;
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(hex_button_);
  buttons->addWidget(inflated_button_);
  buttons->addStretch(1);
  layout()->addItem(buttons);

  connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &DecodeTraceInspector::onSelectionChanged);
  connect(hex_button_, &QPushButton::clicked, this, [this] {
    const auto row = activeRow();
    if (!row.has_value()) {
      return;
    }
    const auto* step = model_->stepAt(*row);
    if (step == nullptr) {
      return;
    }
    auto target = hexTargetFor(*step);
    if (!target.has_value() || !target->valid()) {
      return;
    }
    if (selection_store_ != nullptr) {
      selection_store_->applyNavigation(*target);
    } else {
      emit navigationRequested(*target);
    }
  });
  connect(inflated_button_, &QPushButton::clicked, this, [this] {
    const auto row = activeRow();
    if (!row.has_value()) {
      return;
    }
    const auto* step = model_->stepAt(*row);
    if (step == nullptr) {
      return;
    }
    auto target = inflatedTargetFor(*step);
    if (!target.has_value() || !target->valid()) {
      return;
    }
    if (selection_store_ != nullptr) {
      selection_store_->applyNavigation(*target);
    } else {
      emit navigationRequested(*target);
    }
  });
}

void DecodeTraceInspector::showEvent(QShowEvent* event) {
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
}

void DecodeTraceInspector::setView(
    const pnga::analysis_engine::DecodeTraceInspectorView& view) {
  // Per-document refit policy: the page publishes on every pixel click
  // within one document, so the content-derived initial widths are
  // re-derived only when the published generation changes (document open or
  // close); every same-generation publish preserves manual widths.
  const bool generation_changed =
      view.scope.generation != view_.scope.generation;
  view_ = view;
  model_->setView(
      std::make_shared<const pnga::analysis_engine::DecodeTraceInspectorView>(
          view));
  if (generation_changed) {
    table_->resizeColumnsToContents();
  }
  updateScopeHeading();
  updateButtons();
  updateDetails();
  // A fresh publish must reveal the row the user is working with: the manual
  // row selection when one exists, otherwise the event that contains the
  // Current output byte. scrollTo never creates a selection (Current ≠
  // Selection, flow-ui §20.6) and is a no-op for a row that is already fully
  // visible, so repeated publishes stay deterministic. The model reset
  // defers the view/header layout to the next event loop pass; flush it
  // synchronously so the scroll targets real section geometry instead of a
  // stale empty header.
  if (const auto row = activeRow(); row.has_value()) {
    table_->doItemsLayout();
    table_->scrollTo(model_->index(*row, 0));
  }
}

void DecodeTraceInspector::setExternalStatus(const QString& /*text*/) {
  // WP-5U12: the shared Compression context owns the trace status.
}

void DecodeTraceInspector::clear() {
  setView(pnga::analysis_engine::DecodeTraceInspectorView{});
}

void DecodeTraceInspector::setSelectionStore(CompressionSelectionStore* store) {
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
        selection_store_, &CompressionSelectionStore::stateChanged, this,
        [this](const pnga::trace_model::CompressionSelectionState& state) {
          setSelectionState(state);
        });
    setSelectionState(selection_store_->state());
  }
}

void DecodeTraceInspector::setSelectionState(
    const pnga::trace_model::CompressionSelectionState& state) {
  // A state from another generation highlights nothing and never steers the
  // details away from the published bounded result.
  if (state.generation == view_.scope.generation) {
    model_->setSelectionState(state);
  } else {
    model_->setSelectionState(
        pnga::trace_model::CompressionSelectionState{});
  }
  updateButtons();
  updateDetails();
}

std::uint64_t DecodeTraceInspector::nextRequestSerial() const noexcept {
  // Page-local monotonic serial. The page pointer forms the high bits so
  // several pages can emit into one store without serial collisions.
  return serial_base_ + (++serial_counter_);
}

std::optional<int> DecodeTraceInspector::activeRow() const noexcept {
  const auto rows = table_->selectionModel()->selectedRows();
  if (rows.size() == 1 && model_->stepAt(rows.front().row()) != nullptr) {
    return rows.front().row();
  }
  // Without a manual selection the details and actions follow the event
  // that contains the Current output byte.
  for (int row = 0; row < model_->rowCount(); ++row) {
    const auto* step = model_->stepAt(row);
    if (step != nullptr && step->contains_current) {
      return row;
    }
  }
  return std::nullopt;
}

std::optional<pnga::trace_model::CompressionNavigationTarget>
DecodeTraceInspector::manualTargetFor(
    const pnga::analysis_engine::DecodeTraceStep& step) const noexcept {
  if (!step.input_range.valid() || step.input_range.empty() ||
      step.physical_input_spans.empty()) {
    return std::nullopt;
  }
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = view_.scope.generation;
  target.request_serial = nextRequestSerial();
  target.source_unit = pnga::trace_model::DocumentSourceUnit{};
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kDecodeTrace;
  target.logical_range = step.input_range;
  target.physical_spans = step.physical_input_spans;
  if (step.block_index >= 0) {
    target.block_index = static_cast<std::uint64_t>(step.block_index);
  }
  target.token_index = step.token_index;
  return target;
}

std::optional<pnga::trace_model::CompressionNavigationTarget>
DecodeTraceInspector::hexTargetFor(
    const pnga::analysis_engine::DecodeTraceStep& step) const noexcept {
  // Show in Hex targets the compressed input: the precise DeflateBitRange
  // plus every ordered physical file span; no span, no navigation.
  if (!step.input_range.valid() || step.input_range.empty() ||
      step.physical_input_spans.empty()) {
    return std::nullopt;
  }
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = view_.scope.generation;
  target.request_serial = nextRequestSerial();
  target.source_unit = pnga::trace_model::DocumentSourceUnit{};
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kDecodeTrace;
  target.logical_range = step.input_range;
  target.physical_spans = step.physical_input_spans;
  if (step.block_index >= 0) {
    target.block_index = static_cast<std::uint64_t>(step.block_index);
  }
  target.token_index = step.token_index;
  return target;
}

std::optional<pnga::trace_model::CompressionNavigationTarget>
DecodeTraceInspector::inflatedTargetFor(
    const pnga::analysis_engine::DecodeTraceStep& step) const noexcept {
  // Show inflated output targets the inflated bytes: only the typed
  // InflatedByteRange, never a compressed scalar or file span.
  if (!step.output_range.valid() || step.output_range.empty()) {
    return std::nullopt;
  }
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = view_.scope.generation;
  target.request_serial = nextRequestSerial();
  target.source_unit = pnga::trace_model::DocumentSourceUnit{};
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kDecodeTrace;
  target.logical_range = step.output_range;
  if (step.block_index >= 0) {
    target.block_index = static_cast<std::uint64_t>(step.block_index);
  }
  target.token_index = step.token_index;
  return target;
}

void DecodeTraceInspector::onSelectionChanged() {
  const auto row = activeRow();
  if (row.has_value()) {
    const auto* step = model_->stepAt(*row);
    if (step != nullptr) {
      // Row selection changes only Manual Selection: no history entry, no
      // navigation request and no trace submission.
      if (auto target = manualTargetFor(*step);
          target.has_value() && target->valid()) {
        if (selection_store_ != nullptr) {
          selection_store_->setManual(*target);
        } else {
          emit navigationRequested(*target);
        }
      }
    }
  }
  updateButtons();
  updateDetails();
}

void DecodeTraceInspector::updateScopeHeading() {
  const auto& scope = view_.scope;
  if (scope.status == pnga::analysis_engine::TraceQueryStatus::kNotIndexed &&
      view_.steps.empty()) {
    // Nothing published yet; the shared Compression context owns the
    // loading/empty copy.
    scope_heading_->setText(QString());
    return;
  }
  QString status_word;
  switch (scope.status) {
    case pnga::analysis_engine::TraceQueryStatus::kReady:
      status_word = QStringLiteral("ready");
      break;
    case pnga::analysis_engine::TraceQueryStatus::kPartial:
      status_word = QStringLiteral("partial");
      break;
    case pnga::analysis_engine::TraceQueryStatus::kError:
      status_word = QStringLiteral("error");
      break;
    case pnga::analysis_engine::TraceQueryStatus::kCancelled:
      status_word = QStringLiteral("cancelled");
      break;
    case pnga::analysis_engine::TraceQueryStatus::kReplaying:
      status_word = QStringLiteral("replaying");
      break;
    case pnga::analysis_engine::TraceQueryStatus::kNotIndexed:
      status_word = QStringLiteral("not indexed");
      break;
  }
  QString text =
      QStringLiteral("Bounded trace · output bytes %1–%2 · %3 events · %4")
          .arg(static_cast<qulonglong>(scope.requested_output.begin.value))
          .arg(static_cast<qulonglong>(scope.requested_output.end.value))
          .arg(static_cast<qulonglong>(scope.returned_token_count))
          .arg(status_word);
  if (scope.truncated) {
    text += QStringLiteral(" · truncated");
  }
  if (!scope.stop_reason.empty()) {
    text += QStringLiteral(": %1")
                .arg(QString::fromStdString(scope.stop_reason));
  }
  scope_heading_->setText(text);
}

void DecodeTraceInspector::updateButtons() {
  const auto row = activeRow();
  const auto* step = row.has_value() ? model_->stepAt(*row) : nullptr;
  hex_button_->setEnabled(step != nullptr && step->input_range.valid() &&
                          !step->input_range.empty() &&
                          !step->physical_input_spans.empty());
  inflated_button_->setEnabled(step != nullptr && step->output_range.valid() &&
                               !step->output_range.empty());
}

void DecodeTraceInspector::updateDetails() {
  if (view_.steps.empty()) {
    switch (view_.scope.status) {
      case pnga::analysis_engine::TraceQueryStatus::kReplaying:
        setDetailsInstruction(
            QStringLiteral("Analyzing the zlib/DEFLATE stream…"));
        return;
      case pnga::analysis_engine::TraceQueryStatus::kError:
        setDetailsInstruction(
            view_.scope.stop_reason.empty()
                ? QStringLiteral("Trace stopped.")
                : QStringLiteral("Trace stopped: %1")
                      .arg(QString::fromStdString(view_.scope.stop_reason)));
        return;
      case pnga::analysis_engine::TraceQueryStatus::kPartial:
      case pnga::analysis_engine::TraceQueryStatus::kCancelled:
        setDetailsInstruction(
            view_.scope.stop_reason.empty()
                ? QStringLiteral(
                      "Partial trace · verified tokens are shown above.")
                : QStringLiteral("Partial trace · %1")
                      .arg(QString::fromStdString(view_.scope.stop_reason)));
        return;
      default:
        setDetailsInstruction(QStringLiteral(
            "No tokens in the bounded result for the selected output range."));
        return;
    }
  }
  const auto row = activeRow();
  if (!row.has_value()) {
    setDetailsInstruction(QStringLiteral(
        "Select an event to inspect its decode path, or lock a pixel to find "
        "the current event."));
    return;
  }
  const auto* step = model_->stepAt(*row);
  if (step == nullptr) {
    setDetailsInstruction(QStringLiteral(
        "No tokens in the bounded result for the selected output range."));
    return;
  }
  const QString path = QString::fromLatin1(
      pnga::analysis_engine::decode_trace_path_text(step->path));
  std::vector<std::pair<QString, QString>> details;
  switch (step->path) {
    case pnga::analysis_engine::DecodeTracePath::kLiteral:
      details.emplace_back(QStringLiteral("Huffman symbol"),
                           symbol_label(step->huffman_symbol));
      details.emplace_back(
          QStringLiteral("Literal"),
          QStringLiteral("0x%1")
              .arg(step->literal, 2, 16, QLatin1Char('0')));
      details.emplace_back(QStringLiteral("Input"),
                           QStringLiteral("DEFLATE bits %1")
                               .arg(bits_label(step->input_range)));
      details.emplace_back(QStringLiteral("Output"),
                           QStringLiteral("Inflated bytes %1")
                               .arg(bytes_label(step->output_range)));
      if (step->selected_byte_offset_in_event.has_value()) {
        details.emplace_back(
            QStringLiteral("Current byte"),
            QStringLiteral("event offset +%1")
                .arg(static_cast<qulonglong>(
                    *step->selected_byte_offset_in_event)));
      }
      break;
    case pnga::analysis_engine::DecodeTracePath::kMatch:
      details.emplace_back(QStringLiteral("Literal/length symbol"),
                           symbol_label(step->huffman_symbol));
      details.emplace_back(
          QStringLiteral("Length"),
          QStringLiteral("base %1 + extra %2 (%3 bit) = %4")
              .arg(step->length_base)
              .arg(step->length_extra_value)
              .arg(step->length_extra_bits)
              .arg(step->length));
      details.emplace_back(
          QStringLiteral("Distance"),
          QStringLiteral("base %1 + extra %2 (%3 bit) = %4")
              .arg(step->distance_base)
              .arg(step->distance_extra_value)
              .arg(step->distance_extra_bits)
              .arg(step->distance));
      details.emplace_back(QStringLiteral("Source"),
                           sources_label(step->match_source_ranges));
      details.emplace_back(
          QStringLiteral("Target"),
          QStringLiteral("Inflated bytes [%1, %2)")
              .arg(static_cast<qulonglong>(step->match_target.begin.value))
              .arg(static_cast<qulonglong>(step->match_target.end.value)));
      details.emplace_back(QStringLiteral("Overlapping copy"),
                           step->match_overlaps ? QStringLiteral("yes")
                                                : QStringLiteral("no"));
      details.emplace_back(QStringLiteral("Input"),
                           QStringLiteral("DEFLATE bits %1")
                               .arg(bits_label(step->input_range)));
      details.emplace_back(QStringLiteral("Output"),
                           QStringLiteral("Inflated bytes %1")
                               .arg(bytes_label(step->output_range)));
      if (step->selected_byte_offset_in_event.has_value()) {
        QString value = QStringLiteral("match offset +%1")
                            .arg(static_cast<qulonglong>(
                                *step->selected_byte_offset_in_event));
        if (step->selected_byte_source_offset.has_value()) {
          value += QStringLiteral(", source logical offset %1")
                       .arg(static_cast<qulonglong>(
                           *step->selected_byte_source_offset));
        }
        details.emplace_back(QStringLiteral("Current byte"), value);
      }
      break;
    case pnga::analysis_engine::DecodeTracePath::kEndOfBlock:
      details.emplace_back(QStringLiteral("Explanation"),
                           QString::fromStdString(step->event_text));
      details.emplace_back(QStringLiteral("Huffman symbol"),
                           symbol_label(step->huffman_symbol));
      details.emplace_back(QStringLiteral("Input"),
                           QStringLiteral("DEFLATE bits %1")
                               .arg(bits_label(step->input_range)));
      details.emplace_back(QStringLiteral("Output"),
                           QStringLiteral("Inflated bytes %1")
                               .arg(bytes_label(step->output_range)));
      break;
  }
  setDetails(QStringLiteral("Event #%1 · %2")
                 .arg(static_cast<qulonglong>(step->token_index))
                 .arg(path),
             details);
}

}  // namespace pnga::ui::qt
