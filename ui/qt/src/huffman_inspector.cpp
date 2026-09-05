// WP-5U12D Huffman page implementation. Model/view presentation of one
// projected table with typed actions only: row selection changes Manual
// Selection, opening an occurrence applies a typed B navigation to the
// existing bounded token, and nothing here replays, decodes or reverses
// bits.

#include "pnga/ui/qt/huffman_inspector.h"

#include "pnga/ui/qt/compression_selection_store.h"

#include <pnga/trace-model/provenance.h>
#include <pnga/ui/qt/application_theme.h>

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>
#include <QTableView>
#include <QVariant>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace pnga::ui::qt {

namespace {

constexpr std::array<const char*, 3> kKindLabels = {
    "Literal / Length", "Distance", "Code Length"};

pnga::deflate_trace::HuffmanTableKind kind_for_button(int id) noexcept {
  switch (id) {
    case 1:
      return pnga::deflate_trace::HuffmanTableKind::kDistance;
    case 2:
      return pnga::deflate_trace::HuffmanTableKind::kCodeLength;
    default:
      return pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  }
}

std::optional<int> button_for_kind(
    pnga::deflate_trace::HuffmanTableKind kind) noexcept {
  switch (kind) {
    case pnga::deflate_trace::HuffmanTableKind::kLiteralLength:
      return 0;
    case pnga::deflate_trace::HuffmanTableKind::kDistance:
      return 1;
    case pnga::deflate_trace::HuffmanTableKind::kCodeLength:
      return 2;
  }
  return std::nullopt;
}

QString mode_heading(pnga::analysis_engine::HuffmanTableMode mode) noexcept {
  switch (mode) {
    case pnga::analysis_engine::HuffmanTableMode::kFixed:
      return QStringLiteral("Fixed");
    case pnga::analysis_engine::HuffmanTableMode::kDynamic:
      return QStringLiteral("Dynamic");
    case pnga::analysis_engine::HuffmanTableMode::kStored:
      return QStringLiteral("Stored");
  }
  return QStringLiteral("Unknown");
}

// Row-selection table keyboard contract (flow-ui §14): Up/Down move the
// native row selection and Home/End jump to the first/last row.
class HuffmanTableView final : public QTableView {
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

}  // namespace

HuffmanInspector::HuffmanInspector(QWidget* parent)
    : CompressionInspectorPage(parent) {
  serial_base_ = static_cast<std::uint64_t>(
                     reinterpret_cast<std::uintptr_t>(this))
                 << 16;
  model_ = new HuffmanInspectorModel(this);
  table_ = new HuffmanTableView(this);
  table_->setObjectName(QStringLiteral("compressionHuffmanTable"));
  table_->setAccessibleName(QStringLiteral("Huffman table entries"));
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
  // exact widths the ResizeToContents mode used to produce. The Meaning
  // column keeps the normative Stretch fill mode whose viewport-derived
  // width is locked into the WP-5U12F baselines.
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setSectionResizeMode(HuffmanInspectorModel::Meaning,
                               QHeaderView::Stretch);
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

  heading_ = new QLabel(this);
  heading_->setObjectName(QStringLiteral("huffmanInspectorHeading"));
  heading_->setWordWrap(true);

  // Page-local table selector in the normative order (flow-ui §8.2): the
  // selector scrolls instead of widening the dock at narrow widths.
  auto* selector_scroll = new QScrollArea(this);
  selector_ = selector_scroll;
  selector_->setObjectName(QStringLiteral("huffmanTableKindSelector"));
  selector_->setAccessibleName(QStringLiteral("Huffman table kind"));
  selector_->setMinimumWidth(0);
  selector_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  selector_scroll->setFrameShape(QFrame::NoFrame);
  selector_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  selector_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  selector_scroll->setWidgetResizable(false);
  selector_scroll->setMinimumHeight(28);
  selector_scroll->setMaximumHeight(32);
  auto* selector_content = new QWidget(selector_scroll);
  auto* selector_layout = new QHBoxLayout(selector_content);
  selector_layout->setContentsMargins(0, 0, 0, 0);
  selector_layout->setSpacing(2);
  kind_buttons_ = new QButtonGroup(selector_scroll);
  kind_buttons_->setExclusive(true);
  const std::array<QString, 3> object_names = {
      QStringLiteral("huffmanTableKindLiteralLength"),
      QStringLiteral("huffmanTableKindDistance"),
      QStringLiteral("huffmanTableKindCodeLength")};
  for (int id = 0; id < static_cast<int>(3); ++id) {
    auto* button = new QPushButton(QLatin1String(kKindLabels[static_cast<
                                                       std::size_t>(id)]),
                                   selector_content);
    button->setObjectName(object_names[static_cast<std::size_t>(id)]);
    button->setCheckable(true);
    button->setFlat(true);
    button->setMinimumWidth(0);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setAccessibleName(
        QLatin1String(kKindLabels[static_cast<std::size_t>(id)]) +
        QStringLiteral(" Huffman table"));
    kind_buttons_->addButton(button, id);
    selector_layout->addWidget(button);
  }
  selector_layout->activate();
  selector_content->setFixedSize(selector_layout->sizeHint());
  selector_scroll->setWidget(selector_content);
  kind_buttons_->button(0)->setChecked(true);

  auto* header_row = new QWidget(this);
  auto* header_layout = new QHBoxLayout(header_row);
  header_layout->setContentsMargins(0, 0, 0, 0);
  header_layout->addWidget(heading_, 1);
  header_layout->addWidget(selector_);
  static_cast<QBoxLayout*>(layout())->insertWidget(0, header_row);

  // The occurrence drill-in lives in the details area (flow-ui §8.2): it
  // opens the next bounded occurrence of the selected symbol.
  open_occurrence_button_ =
      new QPushButton(QStringLiteral("Open occurrence"), this);
  open_occurrence_button_->setObjectName(
      QStringLiteral("huffmanOpenOccurrence"));
  open_occurrence_button_->setEnabled(false);
  if (auto* details_host =
          findChild<QWidget*>(QStringLiteral("compressionDetails"));
      details_host != nullptr && details_host->layout() != nullptr) {
    details_host->layout()->addWidget(open_occurrence_button_);
  }

  connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &HuffmanInspector::onSelectionChanged);
  connect(kind_buttons_, &QButtonGroup::idClicked, this, [this](int) {
    syncActiveTable();
    updateDetails();
  });
  connect(open_occurrence_button_, &QPushButton::clicked, this,
          &HuffmanInspector::openOccurrence);
}

void HuffmanInspector::setView(
    const pnga::analysis_engine::HuffmanInspectorView& view) {
  view_ = view;
  occurrence_cursor_ = 0;
  syncActiveTable();
  updateButtons();
  updateDetails();
}

void HuffmanInspector::setExternalStatus(const QString& /*text*/) {
  // WP-5U12: the shared Compression context owns the trace status.
}

void HuffmanInspector::clear() {
  setView(pnga::analysis_engine::HuffmanInspectorView{});
}

void HuffmanInspector::setSelectionStore(CompressionSelectionStore* store) {
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

void HuffmanInspector::setSelectionState(
    const pnga::trace_model::CompressionSelectionState& state) {
  // A state from another generation highlights nothing and never steers the
  // active table.
  if (state.generation == view_.generation) {
    model_->setSelectionState(state);
  } else {
    model_->setSelectionState(
        pnga::trace_model::CompressionSelectionState{});
  }
  syncActiveTable();
  syncSelectionFromState();
  updateButtons();
  updateDetails();
}

std::uint64_t HuffmanInspector::nextRequestSerial() const noexcept {
  // Page-local monotonic serial. The page pointer forms the high bits so
  // several pages can emit into one store without serial collisions.
  return serial_base_ + (++serial_counter_);
}
std::optional<const pnga::analysis_engine::HuffmanInspectorTable*>
HuffmanInspector::activeTable() const noexcept {
  if (view_.tables.empty()) {
    return std::nullopt;
  }
  std::optional<std::uint64_t> block;
  if (selection_store_ != nullptr &&
      selection_store_->state().generation == view_.generation) {
    const auto& state = selection_store_->state();
    if (state.manual.has_value() && state.manual->block_index.has_value()) {
      block = *state.manual->block_index;
    } else if (state.current.has_value() &&
               state.current->block_index.has_value()) {
      block = *state.current->block_index;
    }
  }
  const auto kind = kind_for_button(kind_buttons_->checkedId());
  const auto pick_block = [&](std::uint64_t wanted)
      -> std::optional<
          const pnga::analysis_engine::HuffmanInspectorTable*> {
    const pnga::analysis_engine::HuffmanInspectorTable* first_huffman =
        nullptr;
    const pnga::analysis_engine::HuffmanInspectorTable* stored = nullptr;
    for (const auto& candidate : view_.tables) {
      if (candidate.block_index != wanted) {
        continue;
      }
      if (candidate.mode ==
            pnga::analysis_engine::HuffmanTableMode::kStored) {
        if (stored == nullptr) {
          stored = &candidate;
        }
        continue;
      }
      if (first_huffman == nullptr) {
        first_huffman = &candidate;
      }
      if (candidate.kind.has_value() && *candidate.kind == kind) {
        return &candidate;
      }
    }
    if (first_huffman != nullptr) {
      if (auto id = button_for_kind(*first_huffman->kind); id.has_value()) {
        QSignalBlocker blocker(kind_buttons_);
        kind_buttons_->button(*id)->setChecked(true);
      }
      return first_huffman;
    }
    if (stored != nullptr) {
      return stored;  // the explicit no-Huffman state of this block
    }
    return std::nullopt;
  };
  if (block.has_value()) {
    return pick_block(*block);
  }
  // No block is known: prefer the first huffman table, then a stored one.
  for (const auto& candidate : view_.tables) {
    if (candidate.mode ==
            pnga::analysis_engine::HuffmanTableMode::kStored) {
      continue;
    }
    if (candidate.kind.has_value() && *candidate.kind == kind) {
      return &candidate;
    }
  }
  for (const auto& candidate : view_.tables) {
    if (candidate.mode !=
            pnga::analysis_engine::HuffmanTableMode::kStored) {
      if (auto id = button_for_kind(*candidate.kind); id.has_value()) {
        QSignalBlocker blocker(kind_buttons_);
        kind_buttons_->button(*id)->setChecked(true);
      }
      return &candidate;
    }
  }
  return &view_.tables.front();  // stored-only result
}

const pnga::analysis_engine::HuffmanOccurrenceFact*
HuffmanInspector::occurrenceFact(std::uint64_t token_index) const noexcept {
  for (const auto& fact : view_.occurrences) {
    if (fact.token_index == token_index) {
      return &fact;
    }
  }
  return nullptr;
}

std::optional<pnga::trace_model::CompressionNavigationTarget>
HuffmanInspector::occurrenceTarget(
    const pnga::analysis_engine::HuffmanOccurrenceFact& fact,
    std::uint16_t symbol) const noexcept {
  const auto scope = std::find_if(
      view_.block_scopes.begin(), view_.block_scopes.end(),
      [this](const pnga::analysis_engine::HuffmanBlockScope& candidate) {
        const auto table = activeTable();
        return table.has_value() &&
               candidate.block_index == (*table)->block_index;
      });
  if (scope == view_.block_scopes.end()) {
    return std::nullopt;
  }
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = view_.generation;
  target.request_serial = nextRequestSerial();
  target.source_unit = pnga::trace_model::DocumentSourceUnit{};
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kHuffman;
  // The precise bounded token bits identify the occurrence; end-of-block
  // boundaries carry no bits and fall back to the block's typed scope.
  if (fact.input_range.valid() && !fact.input_range.empty()) {
    target.logical_range = fact.input_range;
  } else if (scope->deflate_range.valid() && !scope->deflate_range.empty()) {
    target.logical_range = scope->deflate_range;
  } else {
    return std::nullopt;
  }
  // Physical spans already mapped in the bounded result, converted in
  // original order with checked arithmetic.
  for (const auto& span : scope->physical_spans) {
    if (span.space != pnga::trace_model::ProvenanceSpace::kPhysicalFile ||
        span.length == 0 ||
        span.length > std::numeric_limits<std::uint64_t>::max() -
                          span.offset) {
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
  target.block_index = scope->block_index;
  target.symbol = symbol;
  target.token_index = fact.token_index;
  return target;
}

std::optional<pnga::trace_model::CompressionNavigationTarget>
HuffmanInspector::symbolTarget(
    const pnga::analysis_engine::HuffmanInspectorEntry& entry) const
    noexcept {
  const auto table = activeTable();
  if (!table.has_value()) {
    return std::nullopt;
  }
  const auto scope = std::find_if(
      view_.block_scopes.begin(), view_.block_scopes.end(),
      [&table](const pnga::analysis_engine::HuffmanBlockScope& candidate) {
        return candidate.block_index == (*table)->block_index;
      });
  if (scope == view_.block_scopes.end() || !scope->deflate_range.valid() ||
      scope->deflate_range.empty()) {
    return std::nullopt;
  }
  pnga::trace_model::CompressionNavigationTarget target;
  target.generation = view_.generation;
  target.request_serial = nextRequestSerial();
  target.source_unit = pnga::trace_model::DocumentSourceUnit{};
  target.origin = pnga::trace_model::CompressionNavigationOrigin::kHuffman;
  // A codebook row has no unique input range; the typed block scope carries
  // the navigation context while the symbol identifies the row.
  target.logical_range = scope->deflate_range;
  for (const auto& span : scope->physical_spans) {
    if (span.space != pnga::trace_model::ProvenanceSpace::kPhysicalFile ||
        span.length == 0 ||
        span.length > std::numeric_limits<std::uint64_t>::max() -
                          span.offset) {
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
  target.block_index = scope->block_index;
  target.symbol = entry.symbol;
  return target;
}

void HuffmanInspector::syncActiveTable() {
  const auto table = activeTable();
  if (!table.has_value()) {
    model_->setTable(nullptr);
    heading_->setText(
        QStringLiteral("No %1 tables in the bounded result")
            .arg(QLatin1String(
                kKindLabels[static_cast<std::size_t>(
                    kind_buttons_->checkedId())])));
    return;
  }
  model_->setTable(
      std::make_shared<const pnga::analysis_engine::HuffmanInspectorTable>(
          **table));
  // Per-document refit policy: content-derived initial widths re-derive when
  // the published generation changes or the table kind switches; a
  // same-generation same-kind republish (row publish, Current change)
  // preserves the user's manual column widths. The table-kind switch
  // publishes through the same path.
  const int kind = kind_buttons_->checkedId();
  if (view_.generation != last_refit_generation_ ||
      kind != last_refit_kind_) {
    table_->resizeColumnsToContents();
    last_refit_generation_ = view_.generation;
    last_refit_kind_ = kind;
  }
  if ((*table)->mode == pnga::analysis_engine::HuffmanTableMode::kStored) {
    heading_->setText(QStringLiteral("Block #%1 · Stored")
                          .arg(static_cast<qulonglong>(
                              (*table)->block_index)));
    return;
  }
  heading_->setText(QStringLiteral("Block #%1 · %2 Huffman")
                        .arg(static_cast<qulonglong>((*table)->block_index))
                        .arg(mode_heading((*table)->mode)));
}

void HuffmanInspector::syncSelectionFromState() {
  if (applying_state_ || selection_store_ == nullptr ||
      selection_store_->state().generation != view_.generation ||
      !selection_store_->state().manual.has_value()) {
    return;
  }
  const auto& manual = *selection_store_->state().manual;
  if (!manual.block_index.has_value() || !manual.symbol.has_value()) {
    return;
  }
  const auto table = activeTable();
  if (!table.has_value() || (*table)->block_index != *manual.block_index) {
    return;
  }
  // Select the row of the manually selected symbol without echoing the
  // state back into the store.
  auto* huffman_model =
      qobject_cast<HuffmanInspectorModel*>(table_->model());
  if (huffman_model == nullptr) {
    return;
  }
  for (int row = 0; row < huffman_model->rowCount(); ++row) {
    const auto* entry = huffman_model->entryAt(row);
    if (entry != nullptr && entry->symbol == *manual.symbol) {
      applying_state_ = true;
      table_->selectRow(row);
      applying_state_ = false;
      return;
    }
  }
}

std::optional<int> HuffmanInspector::activeVisibleRow() const noexcept {
  const auto rows = table_->selectionModel()->selectedRows();
  if (rows.size() == 1) {
    return rows.front().row();
  }
  return std::nullopt;
}

void HuffmanInspector::onSelectionChanged() {
  if (!applying_state_) {
    occurrence_cursor_ = 0;
    const auto row = activeVisibleRow();
    if (row.has_value()) {
      auto* huffman_model =
          qobject_cast<HuffmanInspectorModel*>(table_->model());
      const auto* entry =
          huffman_model != nullptr ? huffman_model->entryAt(*row) : nullptr;
      if (entry != nullptr) {
        if (auto target = symbolTarget(*entry);
            target.has_value() && target->valid()) {
          // Row selection changes only Manual Selection: no history entry,
          // no navigation request and no trace submission.
          if (selection_store_ != nullptr) {
            selection_store_->setManual(*target);
          } else {
            emit navigationRequested(*target);
          }
        }
      }
    }
  }
  updateButtons();
  updateDetails();
}

void HuffmanInspector::openOccurrence() {
  auto* huffman_model = qobject_cast<HuffmanInspectorModel*>(table_->model());
  const auto row = activeVisibleRow();
  const auto* entry =
      row.has_value() && huffman_model != nullptr
          ? huffman_model->entryAt(*row)
          : nullptr;
  if (entry == nullptr) {
    return;
  }
  if (entry->occurrence_token_indices.empty()) {
    updateDetails();
    return;
  }
  const std::size_t position =
      occurrence_cursor_ % entry->occurrence_token_indices.size();
  ++occurrence_cursor_;
  const std::uint64_t token_index =
      entry->occurrence_token_indices[position];
  const auto* fact = occurrenceFact(token_index);
  if (fact == nullptr) {
    return;
  }
  if (auto target = occurrenceTarget(*fact, entry->symbol);
      target.has_value() && target->valid()) {
    if (selection_store_ != nullptr) {
      selection_store_->applyNavigation(*target);
    } else {
      emit navigationRequested(*target);
    }
  }
  updateDetails();
}

void HuffmanInspector::updateButtons() {
  auto* huffman_model = qobject_cast<HuffmanInspectorModel*>(table_->model());
  const auto row = activeVisibleRow();
  const auto* entry =
      row.has_value() && huffman_model != nullptr
          ? huffman_model->entryAt(*row)
          : nullptr;
  open_occurrence_button_->setEnabled(entry != nullptr);
}

void HuffmanInspector::updateDetails() {
  auto* huffman_model = qobject_cast<HuffmanInspectorModel*>(table_->model());
  const auto row = activeVisibleRow();
  const auto* entry =
      row.has_value() && huffman_model != nullptr
          ? huffman_model->entryAt(*row)
          : nullptr;
  if (entry == nullptr) {
    const auto table = activeTable();
    if (!table.has_value()) {
      setDetailsInstruction(
          QStringLiteral("The bounded result contains no %1 Huffman table "
                         "for this table kind.")
              .arg(QLatin1String(kKindLabels[static_cast<std::size_t>(
                  kind_buttons_->checkedId())])));
      return;
    }
    if ((*table)->mode == pnga::analysis_engine::HuffmanTableMode::kStored) {
      setDetails(
          QStringLiteral("Block #%1 · Stored")
              .arg(static_cast<qulonglong>((*table)->block_index)),
          {{QStringLiteral("Explanation"),
            QStringLiteral(
                "Block #%1 is stored without Huffman coding.\n"
                "Inspect its LEN/NLEN and byte range in DEFLATE Blocks.")
                .arg(static_cast<qulonglong>((*table)->block_index))}});
      return;
    }
    setDetailsInstruction(QStringLiteral(
        "Select a table entry to inspect its canonical code, read order and "
        "bounded occurrences."));
    return;
  }

  std::vector<std::pair<QString, QString>> details;
  const auto table = activeTable();
  details.emplace_back(
      QStringLiteral("Table"),
      table.has_value()
          ? QString::fromStdString((*table)->selector_label)
          : QStringLiteral("—"));
  details.emplace_back(
      QStringLiteral("Canonical"),
      entry->canonical_bits.empty()
          ? QStringLiteral("— · %1 bits").arg(entry->bit_length)
          : QStringLiteral("%1 · %2 bits")
                .arg(QString::fromStdString(entry->canonical_bits))
                .arg(entry->bit_length));
  details.emplace_back(
      QStringLiteral("Read order"),
      entry->read_order_bits.empty()
          ? QStringLiteral("— · transmitted least-significant first")
          : QStringLiteral(
                "%1 · transmitted least-significant first")
                .arg(QString::fromStdString(entry->read_order_bits)));
  if (entry->provenance_range.valid() && !entry->provenance_range.empty()) {
    details.emplace_back(
        QStringLiteral("Provenance"),
        QStringLiteral("DEFLATE bits [%1, %2)")
            .arg(static_cast<qulonglong>(
                entry->provenance_range.begin.value))
            .arg(static_cast<qulonglong>(
                entry->provenance_range.end.value)));
  }
  if (!entry->occurrence_token_indices.empty()) {
    QString used_by = QStringLiteral("events ");
    constexpr std::size_t kMaxListed = 8;
    const std::size_t listed =
        std::min(entry->occurrence_token_indices.size(), kMaxListed);
    for (std::size_t i = 0; i < listed; ++i) {
      if (i != 0) {
        used_by += QStringLiteral(", ");
      }
      used_by += QStringLiteral("#%1")
                     .arg(static_cast<qulonglong>(
                         entry->occurrence_token_indices[i]));
    }
    if (listed < entry->occurrence_token_indices.size()) {
      used_by += QStringLiteral(" … (+%1 more)")
                     .arg(static_cast<qulonglong>(
                         entry->occurrence_token_indices.size() - listed));
    }
    details.emplace_back(QStringLiteral("Used by"), used_by);
  } else if (table.has_value()) {
    details.emplace_back(
        QStringLiteral("Used by"),
        QStringLiteral("This symbol is defined but not used by Block #%1.")
            .arg(static_cast<qulonglong>((*table)->block_index)));
  }
  setDetails(QStringLiteral("Symbol %1 details").arg(entry->symbol), details);
}

void HuffmanInspector::showEvent(QShowEvent* event) {
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

}  // namespace pnga::ui::qt
