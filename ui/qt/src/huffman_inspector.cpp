// WP-505B / WP-5U12 Huffman Inspector widget implementation.

#include "pnga/ui/qt/huffman_inspector.h"

#include <pnga/deflate-trace/token_decoder.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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

}  // namespace

HuffmanInspector::HuffmanInspector(QWidget* parent)
    : CompressionInspectorPage(parent) {
  QTableWidget* table = masterTable();
  table->setObjectName(QStringLiteral("huffmanInspectorTable"));
  table->setAccessibleName(QStringLiteral("Huffman table entries"));
  table->setColumnCount(6);
  table->setHorizontalHeaderLabels(
      {QStringLiteral("Current"), QStringLiteral("Build"),
       QStringLiteral("Symbol"), QStringLiteral("Bits"),
       QStringLiteral("Canonical"), QStringLiteral("Definition bits")});

  heading_ = new QLabel(this);
  heading_->setObjectName(QStringLiteral("huffmanInspectorHeading"));
  heading_->setWordWrap(true);

  selector_ = new QComboBox(this);
  selector_->setObjectName(QStringLiteral("huffmanTableKindSelector"));
  selector_->setAccessibleName(QStringLiteral("Huffman table kind"));
  selector_->addItem(QStringLiteral("Code length"));
  selector_->addItem(QStringLiteral("Literal / Length"));
  selector_->addItem(QStringLiteral("Distance"));
  selector_->setCurrentIndex(1);

  auto* header_row = new QWidget(this);
  auto* header_layout = new QHBoxLayout(header_row);
  header_layout->setContentsMargins(0, 0, 0, 0);
  header_layout->addWidget(heading_, 1);
  header_layout->addWidget(selector_);
  static_cast<QBoxLayout*>(layout())->insertWidget(0, header_row);

  connect(selector_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int) {
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
  switch (selector_->currentIndex()) {
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
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("—")));
    table->setItem(row, 2, new QTableWidgetItem(QStringLiteral("LEN/NLEN")));
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
  std::optional<std::size_t> current_row;
  for (const auto* candidate : filtered) {
    if (candidate->entries.empty()) {
      if (rendered++ >= static_cast<std::size_t>(kMaxVisibleRows)) {
        truncated = true;
        break;
      }
      const int row = table->rowCount();
      table->insertRow(row);
      table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("—")));
      table->setItem(row, 1, new QTableWidgetItem(QString::number(
                                 static_cast<qulonglong>(candidate->build_order))));
      table->setItem(row, 2,
                     new QTableWidgetItem(QStringLiteral("predefined")));
      table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("—")));
      table->setItem(row, 4,
                     new QTableWidgetItem(QStringLiteral("%1 entries")
                                              .arg(static_cast<qulonglong>(
                                                  candidate->declared_entry_count))));
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
      auto* current_item = new QTableWidgetItem(
          entry.selected ? QStringLiteral("●") : QString());
      current_item->setTextAlignment(Qt::AlignCenter);
      if (entry.selected) {
        current_item->setData(Qt::AccessibleTextRole, QStringLiteral("Current"));
        current_row = static_cast<std::size_t>(row);
      }
      table->setItem(row, 0, current_item);
      table->setItem(row, 1, new QTableWidgetItem(QString::number(
                                 static_cast<qulonglong>(candidate->build_order))));
      table->setItem(row, 2, new QTableWidgetItem(QString::number(entry.symbol)));
      table->setItem(row, 3, new QTableWidgetItem(QString::number(entry.bit_length)));
      table->setItem(row, 4,
                     new QTableWidgetItem(QStringLiteral("%1 (%2 bits)")
                                              .arg(entry.canonical_code)
                                              .arg(entry.bit_length)));
      table->setItem(row, 5,
                     new QTableWidgetItem(QStringLiteral("%1..%2")
                                              .arg(static_cast<qulonglong>(
                                                  entry.provenance_bit_begin))
                                              .arg(static_cast<qulonglong>(
                                                  entry.provenance_bit_end))));
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
    table->setItem(row, 4, new QTableWidgetItem(QStringLiteral("more entries")));
  }
  if (current_row.has_value()) {
    table->selectRow(static_cast<int>(*current_row));
  }
}

void HuffmanInspector::updateDetails() {
  const auto rows = masterTable()->selectionModel()->selectedRows();
  if (rows.size() != 1) {
    setDetailsInstruction(QStringLiteral(
        "Select a table entry to inspect its canonical code and provenance."));
    return;
  }
  const int table_row = rows.front().row();
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
                .arg(entry.canonical_code)
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
