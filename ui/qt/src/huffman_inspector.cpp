// WP-505B Huffman Inspector widget implementation.

#include "pnga/ui/qt/huffman_inspector.h"

#include <pnga/deflate-trace/token_decoder.h>

#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace pnga::ui::qt {

namespace {

const char* kind_text(
    std::optional<pnga::deflate_trace::HuffmanTableKind> kind) noexcept {
  if (!kind.has_value()) {
    return "LEN/NLEN";
  }
  switch (*kind) {
    case pnga::deflate_trace::HuffmanTableKind::kCodeLength:
      return "Code length";
    case pnga::deflate_trace::HuffmanTableKind::kLiteralLength:
      return "Literal/length";
    case pnga::deflate_trace::HuffmanTableKind::kDistance:
      return "Distance";
  }
  return "Unknown";
}

}  // namespace

HuffmanInspector::HuffmanInspector(QWidget* parent) : QWidget(parent) {
  status_ = new QLabel(QStringLiteral("Huffman trace: no trace"), this);
  status_->setObjectName(QStringLiteral("huffmanInspectorStatus"));
  context_ = new QLabel(QStringLiteral("selected token: — | bits: —"), this);
  context_->setObjectName(QStringLiteral("huffmanInspectorContext"));
  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("huffmanInspectorTable"));
  table_->setColumnCount(8);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Block"), QStringLiteral("Mode"),
       QStringLiteral("Build"), QStringLiteral("Table"),
       QStringLiteral("Symbol"), QStringLiteral("Bits"),
       QStringLiteral("Canonical"), QStringLiteral("Provenance")});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setStretchLastSection(true);
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(status_);
  layout->addWidget(context_);
  layout->addWidget(table_, 1);
}

void HuffmanInspector::setView(
    const pnga::analysis_engine::HuffmanInspectorView& view) {
  view_ = view;
  table_->setRowCount(0);
  status_->setText(QStringLiteral("Huffman trace: %1 (generation %2)%3")
                       .arg(QLatin1String(
                           pnga::analysis_engine::huffman_inspector_status_text(
                               view_.status)))
                       .arg(static_cast<qulonglong>(view_.generation))
                       .arg(view_.error.empty()
                                ? QString{}
                                : QStringLiteral(" — %1")
                                      .arg(QString::fromStdString(view_.error))));
  context_->setText(
      QStringLiteral("selected token: %1 | bits: %2")
          .arg(view_.selected_token_index.has_value()
                   ? QString::number(static_cast<qulonglong>(
                         *view_.selected_token_index))
                   : QStringLiteral("—"))
          .arg(view_.selected_input_bit_begin.has_value() &&
                       view_.selected_input_bit_end.has_value()
                   ? QStringLiteral("%1..%2")
                         .arg(static_cast<qulonglong>(
                             *view_.selected_input_bit_begin))
                         .arg(static_cast<qulonglong>(
                             *view_.selected_input_bit_end))
                   : QStringLiteral("—")));
  for (const auto& table : view_.tables) {
    if (table.entries.empty()) {
      const int row = table_->rowCount();
      table_->insertRow(row);
      table_->setItem(row, 0, new QTableWidgetItem(QString::number(
          static_cast<qulonglong>(table.block_index))));
      table_->setItem(row, 1, new QTableWidgetItem(QLatin1String(
          pnga::analysis_engine::huffman_table_mode_text(table.mode))));
      table_->setItem(row, 2, new QTableWidgetItem(QString::number(
          static_cast<qulonglong>(table.build_order))));
      table_->setItem(row, 3,
                      new QTableWidgetItem(QLatin1String(kind_text(table.kind))));
      table_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1 entries")
                                                       .arg(static_cast<qulonglong>(
                                                           table.declared_entry_count))));
      table_->setItem(row, 5, new QTableWidgetItem(QStringLiteral("—")));
      table_->setItem(row, 6, new QTableWidgetItem(QStringLiteral("—")));
      table_->setItem(row, 7, new QTableWidgetItem(QStringLiteral("predefined")));
      continue;
    }
    for (const auto& entry : table.entries) {
      const int row = table_->rowCount();
      table_->insertRow(row);
      table_->setItem(row, 0, new QTableWidgetItem(QString::number(
          static_cast<qulonglong>(table.block_index))));
      table_->setItem(row, 1, new QTableWidgetItem(QLatin1String(
          pnga::analysis_engine::huffman_table_mode_text(table.mode))));
      table_->setItem(row, 2, new QTableWidgetItem(QString::number(
          static_cast<qulonglong>(table.build_order))));
      table_->setItem(row, 3,
                      new QTableWidgetItem(QLatin1String(kind_text(table.kind))));
      table_->setItem(row, 4, new QTableWidgetItem(QString::number(entry.symbol)));
      table_->setItem(row, 5,
                      new QTableWidgetItem(QString::number(entry.bit_length)));
      table_->setItem(row, 6, new QTableWidgetItem(QStringLiteral("%1 (%2 bits)")
                                                        .arg(entry.canonical_code)
                                                        .arg(entry.bit_length)));
      table_->setItem(row, 7, new QTableWidgetItem(QStringLiteral("%1..%2%3")
                                                        .arg(static_cast<qulonglong>(
                                                            entry.provenance_bit_begin))
                                                        .arg(static_cast<qulonglong>(
                                                            entry.provenance_bit_end))
                                                        .arg(entry.selected
                                                                 ? QStringLiteral("  ← selected")
                                                                 : QString{})));
      if (entry.selected) {
        table_->selectRow(row);
      }
    }
  }
}

void HuffmanInspector::setExternalStatus(const QString& text) {
  status_->setText(text);
}

void HuffmanInspector::clear() {
  setView(pnga::analysis_engine::HuffmanInspectorView{});
}

}  // namespace pnga::ui::qt
