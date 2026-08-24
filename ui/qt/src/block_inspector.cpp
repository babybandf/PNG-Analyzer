// WP-505A / WP-5U12 Block Inspector widget implementation.

#include "pnga/ui/qt/block_inspector.h"

#include <pnga/trace-model/provenance.h>

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <limits>

namespace pnga::ui::qt {

namespace {

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
    std::uint64_t end = 0;
    if (span.length > std::numeric_limits<std::uint64_t>::max() -
                          span.offset) {
      out += QStringLiteral("file[overflow)");
      continue;
    }
    end = span.offset + span.length;
    out += QStringLiteral("file[%1..%2)")
               .arg(static_cast<qulonglong>(span.offset))
               .arg(static_cast<qulonglong>(end));
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
  QTableWidget* table = masterTable();
  table->setObjectName(QStringLiteral("blockInspectorTable"));
  table->setAccessibleName(QStringLiteral("Associated DEFLATE blocks"));
  table->setColumnCount(6);
  table->setHorizontalHeaderLabels(
      {QStringLiteral("Current"), QStringLiteral("#"),
       QStringLiteral("Type"), QStringLiteral("Final"),
       QStringLiteral("Input bits"), QStringLiteral("Output bytes")});

  hex_button_ = new QPushButton(QStringLiteral("Show in Hex"), this);
  hex_button_->setObjectName(QStringLiteral("blockShowInHex"));
  deflate_button_ =
      new QPushButton(QStringLiteral("Show in DEFLATE"), this);
  deflate_button_->setObjectName(QStringLiteral("blockShowInDeflate"));
  hex_button_->setEnabled(false);
  deflate_button_->setEnabled(false);
  auto* buttons = new QHBoxLayout;
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(hex_button_);
  buttons->addWidget(deflate_button_);
  buttons->addStretch(1);
  layout()->addItem(buttons);

  connect(table, &QTableWidget::itemSelectionChanged, this,
          &BlockInspector::onSelectionChanged);
  connect(hex_button_, &QPushButton::clicked, this,
          &BlockInspector::showSelectedInHex);
  connect(deflate_button_, &QPushButton::clicked, this,
          &BlockInspector::showSelectedInDeflate);
}

void BlockInspector::setView(
    const pnga::analysis_engine::BlockInspectorView& view) {
  view_ = view;
  QTableWidget* table = masterTable();
  table->setRowCount(0);
  std::size_t rendered = 0;
  for (const auto& row : view_.rows) {
    if (rendered++ >= static_cast<std::size_t>(kMaxVisibleRows)) {
      break;
    }
    const int table_row = table->rowCount();
    table->insertRow(table_row);
    const bool current = row.current_output_position.has_value();
    auto* current_item = new QTableWidgetItem(
        current ? QStringLiteral("●") : QString());
    current_item->setTextAlignment(Qt::AlignCenter);
    if (current) {
      current_item->setData(Qt::AccessibleTextRole, QStringLiteral("Current"));
    }
    table->setItem(table_row, 0, current_item);
    table->setItem(table_row, 1,
                   new QTableWidgetItem(QString::number(
                       static_cast<qulonglong>(row.block_index))));
    table->setItem(table_row, 2,
                   new QTableWidgetItem(QLatin1String(
                       pnga::deflate_index::block_type_text(row.type))));
    table->setItem(table_row, 3,
                   new QTableWidgetItem(row.last ? QStringLiteral("yes")
                                                 : QStringLiteral("no")));
    table->setItem(
        table_row, 4,
        new QTableWidgetItem(QStringLiteral("%1..%2")
                                 .arg(static_cast<qulonglong>(
                                     row.input_bit_begin))
                                 .arg(static_cast<qulonglong>(
                                     row.input_bit_end))));
    table->setItem(
        table_row, 5,
        new QTableWidgetItem(QStringLiteral("%1..%2")
                                 .arg(static_cast<qulonglong>(row.output_begin))
                                 .arg(static_cast<qulonglong>(row.output_end))));
  }
  if (view_.rows.size() > static_cast<std::size_t>(kMaxVisibleRows)) {
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("…")));
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("truncated")));
    table->setItem(row, 5, new QTableWidgetItem(QStringLiteral("%1 more rows")
                                                    .arg(static_cast<qulonglong>(
                                                        view_.rows.size() -
                                                        kMaxVisibleRows))));
  }
  updateButtons();
  updateDetails();
}

void BlockInspector::setExternalStatus(const QString& /*text*/) {
  // WP-5U12: the shared Compression context owns the trace status.
}

void BlockInspector::clear() {
  setView(pnga::analysis_engine::BlockInspectorView{});
}

std::optional<std::size_t> BlockInspector::activeRow() const noexcept {
  const auto rows = masterTable()->selectionModel()->selectedRows();
  if (rows.size() == 1 &&
      rows.front().row() < static_cast<int>(view_.rows.size())) {
    return static_cast<std::size_t>(rows.front().row());
  }
  for (std::size_t i = 0; i < view_.rows.size(); ++i) {
    if (view_.rows[i].current_output_position.has_value()) {
      return i;
    }
  }
  return std::nullopt;
}

void BlockInspector::onSelectionChanged() {
  updateButtons();
  updateDetails();
}

void BlockInspector::updateButtons() {
  const std::optional<std::size_t> row = activeRow();
  hex_button_->setEnabled(row.has_value() &&
                          !view_.rows[*row].physical_spans.empty());
  deflate_button_->setEnabled(row.has_value());
}

void BlockInspector::updateDetails() {
  if (view_.rows.empty()) {
    if (view_.status == pnga::analysis_engine::BlockInspectorStatus::kError &&
        !view_.error.empty()) {
      setDetailsInstruction(QStringLiteral("Trace stopped: %1")
                                .arg(QString::fromStdString(view_.error)));
    } else if (view_.status ==
               pnga::analysis_engine::BlockInspectorStatus::kPartial) {
      setDetailsInstruction(QStringLiteral(
          "Partial trace · verified blocks are shown above."));
    } else {
      setDetailsInstruction(QStringLiteral(
          "No associated DEFLATE blocks for the selected output range."));
    }
    return;
  }
  const std::optional<std::size_t> row = activeRow();
  if (!row.has_value()) {
    setDetailsInstruction(QStringLiteral(
        "Select a block to inspect its provenance, or lock a pixel to find "
        "the current block."));
    return;
  }
  const auto& block = view_.rows[*row];
  std::vector<std::pair<QString, QString>> details;
  details.emplace_back(QStringLiteral("Type"),
                       QString::fromLatin1(
                           pnga::deflate_index::block_type_text(block.type)));
  details.emplace_back(
      QStringLiteral("Final"),
      block.last ? QStringLiteral("yes · BFINAL=1")
                 : QStringLiteral("no · BFINAL=0"));
  details.emplace_back(
      QStringLiteral("Input"),
      QStringLiteral("DEFLATE bits %1")
          .arg(range_text(block.input_bit_begin, block.input_bit_end)));
  details.emplace_back(
      QStringLiteral("Output"),
      QStringLiteral("Inflated bytes %1")
          .arg(range_text(block.output_begin, block.output_end)));
  details.emplace_back(
      QStringLiteral("Current"),
      block.current_output_position.has_value()
          ? QStringLiteral("output byte %1")
                .arg(static_cast<qulonglong>(
                    *block.current_output_position))
          : QStringLiteral("—"));
  details.emplace_back(QStringLiteral("IDAT spans"),
                       span_text(block.physical_spans));
  details.emplace_back(
      QStringLiteral("Scanline"),
      view_.scanline.has_value()
          ? QString::number(static_cast<qulonglong>(*view_.scanline))
          : QStringLiteral("—"));
  setDetails(QStringLiteral("Block #%1 details")
                 .arg(static_cast<qulonglong>(block.block_index)),
             details);
}

void BlockInspector::showSelectedInHex() {
  const std::optional<std::size_t> row = activeRow();
  if (!row.has_value()) {
    return;
  }
  const auto& spans = view_.rows[*row].physical_spans;
  if (!spans.empty()) {
    emit showInHexRequested(static_cast<quint64>(spans.front().offset),
                            static_cast<quint64>(spans.front().length));
  }
}

void BlockInspector::showSelectedInDeflate() {
  const std::optional<std::size_t> row = activeRow();
  if (!row.has_value()) {
    return;
  }
  const auto& block = view_.rows[*row];
  emit showInDeflateRequested(static_cast<quint64>(block.input_bit_begin),
                              static_cast<quint64>(block.input_bit_end));
}

}  // namespace pnga::ui::qt
