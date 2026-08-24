// WP-505B / WP-5U12 Huffman Inspector widget implementation.

#include "pnga/ui/qt/huffman_inspector.h"

#include <pnga/deflate-trace/token_decoder.h>

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QBrush>
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <array>

namespace pnga::ui::qt {

namespace {

QString kind_label(
    std::optional<pnga::deflate_trace::HuffmanTableKind> kind) noexcept {
  if (!kind.has_value()) {
    return QStringLiteral("LEN/NLEN");
  }
  switch (*kind) {
    case pnga::deflate_trace::HuffmanTableKind::kCodeLength:
      return QStringLiteral("Code length");
    case pnga::deflate_trace::HuffmanTableKind::kLiteralLength:
      return QStringLiteral("Literal / Length");
    case pnga::deflate_trace::HuffmanTableKind::kDistance:
      return QStringLiteral("Distance");
  }
  return QStringLiteral("Unknown");
}

QString mode_label(pnga::analysis_engine::HuffmanTableMode mode) noexcept {
  return QString::fromLatin1(
      pnga::analysis_engine::huffman_table_mode_text(mode));
}

struct SymbolRange {
  std::uint16_t base;
  std::uint8_t extra;
};

constexpr std::array<SymbolRange, 29> kLengthRanges{{
    {3, 0},   {4, 0},   {5, 0},   {6, 0},   {7, 0},   {8, 0},   {9, 0},
    {10, 0},  {11, 1},  {13, 1},  {15, 1},  {17, 1},  {19, 2},  {23, 2},
    {27, 2},  {31, 2},  {35, 3},  {43, 3},  {51, 3},  {59, 3},  {67, 4},
    {83, 4},  {99, 4},  {115, 4}, {131, 5}, {163, 5}, {195, 5}, {227, 5},
    {258, 0},
}};

constexpr std::array<SymbolRange, 30> kDistanceRanges{{
    {1, 0},    {2, 0},    {3, 0},    {4, 0},    {5, 1},    {7, 1},
    {9, 2},    {13, 2},   {17, 3},   {25, 3},   {33, 4},   {49, 4},
    {65, 5},   {97, 5},   {129, 6},  {193, 6},  {257, 7},  {385, 7},
    {513, 8},  {769, 8},  {1025, 9}, {1537, 9}, {2049, 10}, {3073, 10},
    {4097, 11}, {6145, 11}, {8193, 12}, {12289, 12}, {16385, 13},
    {24577, 13},
}};

QString range_label(std::uint16_t base, std::uint8_t extra) {
  const std::uint32_t maximum =
      static_cast<std::uint32_t>(base) + ((1u << extra) - 1u);
  if (maximum == base) {
    return QString::number(base);
  }
  return QStringLiteral("%1-%2").arg(base).arg(maximum);
}

QString symbol_meaning(pnga::deflate_trace::HuffmanTableKind kind,
                       std::uint16_t symbol) {
  switch (kind) {
    case pnga::deflate_trace::HuffmanTableKind::kCodeLength:
      if (symbol <= 15) {
        return QStringLiteral("code length %1").arg(symbol);
      }
      if (symbol == 16) {
        return QStringLiteral("repeat previous length 3-6");
      }
      if (symbol == 17) {
        return QStringLiteral("repeat zero length 3-10");
      }
      if (symbol == 18) {
        return QStringLiteral("repeat zero length 11-138");
      }
      return QStringLiteral("reserved");
    case pnga::deflate_trace::HuffmanTableKind::kLiteralLength:
      if (symbol <= 255) {
        return QStringLiteral("literal %1").arg(symbol);
      }
      if (symbol == 256) {
        return QStringLiteral("end-of-block");
      }
      if (symbol >= 257 && symbol <= 285) {
        const auto& range = kLengthRanges[symbol - 257];
        return QStringLiteral("length %1").arg(range_label(range.base,
                                                              range.extra));
      }
      return QStringLiteral("reserved");
    case pnga::deflate_trace::HuffmanTableKind::kDistance:
      if (symbol < kDistanceRanges.size()) {
        const auto& range = kDistanceRanges[symbol];
        return QStringLiteral("distance %1").arg(range_label(range.base,
                                                               range.extra));
      }
      return QStringLiteral("reserved");
  }
  return QStringLiteral("reserved");
}

QString canonical_bits(const pnga::analysis_engine::HuffmanInspectorEntry& entry) {
  if (entry.bit_length == 0) {
    return QStringLiteral("—");
  }
  return QString::number(entry.canonical_code, 2)
      .rightJustified(entry.bit_length, QLatin1Char('0'));
}

void markAssociatedRow(QTableWidget* table, int row) {
  const QBrush background(QColor(QStringLiteral("#FFF4CC")));
  const QBrush foreground(QColor(QStringLiteral("#4A3B00")));
  for (int column = 0; column < table->columnCount(); ++column) {
    if (auto* item = table->item(row, column); item != nullptr) {
      item->setBackground(background);
      item->setForeground(foreground);
    }
  }
}

}  // namespace

HuffmanInspector::HuffmanInspector(QWidget* parent)
    : CompressionInspectorPage(parent) {
  QTableWidget* table = masterTable();
  table->setObjectName(QStringLiteral("huffmanInspectorTable"));
  table->setAccessibleName(QStringLiteral("Huffman table entries"));
  table->setColumnCount(6);
  table->setHorizontalHeaderLabels(
      {QStringLiteral("Build"), QStringLiteral("Symbol"),
       QStringLiteral("Meaning"), QStringLiteral("Bits"),
       QStringLiteral("Canonical"), QStringLiteral("Definition bits")});

  heading_ = new QLabel(this);
  heading_->setObjectName(QStringLiteral("huffmanInspectorHeading"));
  heading_->setWordWrap(true);

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
  const std::array<QString, 3> labels = {
      QStringLiteral("Code Length"), QStringLiteral("Literal / Length"),
      QStringLiteral("Distance")};
  const std::array<QString, 3> object_names = {
      QStringLiteral("huffmanTableKindCodeLength"),
      QStringLiteral("huffmanTableKindLiteralLength"),
      QStringLiteral("huffmanTableKindDistance")};
  for (int id = 0; id < static_cast<int>(labels.size()); ++id) {
    auto* button = new QPushButton(labels[static_cast<std::size_t>(id)],
                                   selector_content);
    button->setObjectName(object_names[static_cast<std::size_t>(id)]);
    button->setCheckable(true);
    button->setFlat(true);
    button->setMinimumWidth(0);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setAccessibleName(labels[static_cast<std::size_t>(id)] +
                              QStringLiteral(" Huffman table"));
    kind_buttons_->addButton(button, id);
    selector_layout->addWidget(button);
  }
  selector_layout->activate();
  selector_content->setFixedSize(selector_layout->sizeHint());
  selector_scroll->setWidget(selector_content);
  kind_buttons_->button(1)->setChecked(true);

  auto* header_row = new QWidget(this);
  auto* header_layout = new QHBoxLayout(header_row);
  header_layout->setContentsMargins(0, 0, 0, 0);
  header_layout->addWidget(heading_, 1);
  header_layout->addWidget(selector_);
  static_cast<QBoxLayout*>(layout())->insertWidget(0, header_row);

  connect(kind_buttons_, &QButtonGroup::idClicked, this, [this](int) {
    rebuildTable();
    updateDetails();
  });
}

void HuffmanInspector::setView(
    const pnga::analysis_engine::HuffmanInspectorView& view) {
  view_ = view;
  rebuildTable();
  updateDetails();
}

void HuffmanInspector::setExternalStatus(const QString& /*text*/) {
  // WP-5U12: the shared Compression context owns the trace status.
}

void HuffmanInspector::clear() {
  setView(pnga::analysis_engine::HuffmanInspectorView{});
}

pnga::deflate_trace::HuffmanTableKind HuffmanInspector::currentKind()
    const noexcept {
  switch (kind_buttons_ == nullptr ? 1 : kind_buttons_->checkedId()) {
    case 0:
      return pnga::deflate_trace::HuffmanTableKind::kCodeLength;
    case 2:
      return pnga::deflate_trace::HuffmanTableKind::kDistance;
    default:
      return pnga::deflate_trace::HuffmanTableKind::kLiteralLength;
  }
}

void HuffmanInspector::rebuildTable() {
  QTableWidget* table = masterTable();
  table->setRowCount(0);
  associated_row_ = -1;

  bool has_stored = false;
  for (const auto& candidate : view_.tables) {
    if (candidate.mode == pnga::analysis_engine::HuffmanTableMode::kStored) {
      has_stored = true;
      break;
    }
  }
  if (has_stored) {
    heading_->setText(QStringLiteral("Stored block · no Huffman tables"));
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("—")));
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("LEN/NLEN")));
    table->setItem(row, 2,
                   new QTableWidgetItem(QStringLiteral("stored block fields")));
    table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("—")));
    table->setItem(row, 4, new QTableWidgetItem(QStringLiteral("—")));
    table->setItem(row, 5, new QTableWidgetItem(QStringLiteral("2 fields")));
    table->selectRow(row);
    return;
  }

  const pnga::deflate_trace::HuffmanTableKind kind = currentKind();
  std::vector<const pnga::analysis_engine::HuffmanInspectorTable*> filtered;
  for (const auto& candidate : view_.tables) {
    if (candidate.kind.has_value() && *candidate.kind == kind) {
      filtered.push_back(&candidate);
    }
  }
  if (filtered.empty()) {
    heading_->setText(
        QStringLiteral("No %1 tables in the bounded result")
            .arg(kind_label(kind)));
    setDetailsInstruction(QStringLiteral(
        "The bounded result contains no %1 Huffman tables for this table "
        "kind.")
                              .arg(kind_label(kind)));
    return;
  }
  heading_->setText(QStringLiteral("Block #%1 · %2")
                        .arg(static_cast<qulonglong>(filtered.front()->block_index))
                        .arg(mode_label(filtered.front()->mode)));

  std::size_t rendered = 0;
  bool truncated = false;
  for (const auto* candidate : filtered) {
    if (candidate->entries.empty()) {
      if (rendered++ >= static_cast<std::size_t>(kMaxVisibleRows)) {
        truncated = true;
        break;
      }
      const int row = table->rowCount();
      table->insertRow(row);
      table->setItem(row, 0, new QTableWidgetItem(QString::number(
                                 static_cast<qulonglong>(candidate->build_order))));
      table->setItem(row, 1,
                     new QTableWidgetItem(QStringLiteral("predefined")));
      table->setItem(row, 2,
                     new QTableWidgetItem(QStringLiteral("predefined table")));
      table->setItem(row, 3,
                     new QTableWidgetItem(QStringLiteral("%1 entries")
                                              .arg(static_cast<qulonglong>(
                                                  candidate->declared_entry_count))));
      table->setItem(row, 4, new QTableWidgetItem(QStringLiteral("—")));
      table->setItem(row, 5, new QTableWidgetItem(QStringLiteral("—")));
      continue;
    }
    for (const auto& entry : candidate->entries) {
      if (entry.bit_length == 0) {
        continue;  // hide unused symbols; declared count remains in details.
      }
      if (rendered++ >= static_cast<std::size_t>(kMaxVisibleRows)) {
        truncated = true;
        break;
      }
      const int row = table->rowCount();
      table->insertRow(row);
      if (entry.selected) {
        associated_row_ = row;
      }
      table->setItem(row, 0, new QTableWidgetItem(QString::number(
                                 static_cast<qulonglong>(candidate->build_order))));
      table->setItem(row, 1, new QTableWidgetItem(QString::number(entry.symbol)));
      table->setItem(
          row, 2,
          new QTableWidgetItem(symbol_meaning(kind, entry.symbol)));
      table->setItem(row, 3,
                     new QTableWidgetItem(QString::number(entry.bit_length)));
      table->setItem(row, 4,
                     new QTableWidgetItem(canonical_bits(entry)));
      table->setItem(row, 5,
                     new QTableWidgetItem(QStringLiteral("%1～%2")
                                              .arg(static_cast<qulonglong>(
                                                  entry.provenance_bit_begin))
                                              .arg(static_cast<qulonglong>(
                                                entry.provenance_bit_end))));
      if (entry.selected) {
        markAssociatedRow(table, row);
      }
    }
    if (truncated) {
      break;
    }
  }
  if (truncated) {
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("…")));
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("truncated")));
    table->setItem(row, 5, new QTableWidgetItem(QStringLiteral("more entries")));
  }
}

void HuffmanInspector::updateDetails() {
  const auto rows = masterTable()->selectionModel()->selectedRows();
  const int table_row = rows.size() == 1 ? rows.front().row() : associated_row_;
  if (table_row < 0) {
    setDetailsInstruction(QStringLiteral(
        "Select a table entry to inspect its canonical code and provenance."));
    return;
  }
  const pnga::deflate_trace::HuffmanTableKind kind = currentKind();
  std::size_t entry_cursor = 0;
  for (const auto& candidate : view_.tables) {
    if (candidate.mode == pnga::analysis_engine::HuffmanTableMode::kStored) {
      if (table_row == 0) {
        setDetails(QStringLiteral("Stored block"),
                   {{QStringLiteral("Explanation"),
                     QStringLiteral("Stored blocks do not use Huffman coding.")},
                    {QStringLiteral("Fields"),
                     QStringLiteral("LEN / NLEN")}});
        return;
      }
      continue;
    }
    if (!candidate.kind.has_value() || *candidate.kind != kind) {
      continue;
    }
    for (std::size_t i = 0; i < candidate.entries.size(); ++i) {
      if (candidate.entries[i].bit_length == 0) {
        continue;
      }
      if (entry_cursor == static_cast<std::size_t>(table_row)) {
        const auto& entry = candidate.entries[i];
        std::vector<std::pair<QString, QString>> details;
        details.emplace_back(QStringLiteral("Table"),
                             kind_label(candidate.kind));
        details.emplace_back(
            QStringLiteral("Canonical"),
            QStringLiteral("%1 · %2 bits")
                .arg(canonical_bits(entry))
                .arg(entry.bit_length));
        details.emplace_back(
            QStringLiteral("Read order"),
            QStringLiteral("%1 · wire LSB-first")
                .arg(entry.read_order_code));
        details.emplace_back(
            QStringLiteral("Provenance"),
            QStringLiteral("DEFLATE bits [%1, %2)")
                .arg(static_cast<qulonglong>(entry.provenance_bit_begin))
                .arg(static_cast<qulonglong>(entry.provenance_bit_end)));
        details.emplace_back(
            QStringLiteral("Relation"),
            entry.selected
                ? QStringLiteral("entry associated with selected token")
                : QStringLiteral("—"));
        setDetails(QStringLiteral("Symbol %1 details").arg(entry.symbol),
                   details);
        return;
      }
      ++entry_cursor;
    }
    if (candidate.entries.empty()) {
      if (entry_cursor == static_cast<std::size_t>(table_row)) {
        setDetails(
            QStringLiteral("%1 table").arg(kind_label(candidate.kind)),
            {{QStringLiteral("Mode"), mode_label(candidate.mode)},
             {QStringLiteral("Capacity"),
              QStringLiteral("%1 entries")
                  .arg(static_cast<qulonglong>(
                      candidate.declared_entry_count))}});
        return;
      }
      ++entry_cursor;
    }
  }
  setDetailsInstruction(QStringLiteral(
      "Select a table entry to inspect its canonical code and provenance."));
}

}  // namespace pnga::ui::qt
