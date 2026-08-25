// WP-505A / WP-5U12 Block Inspector widget implementation.

#include "pnga/ui/qt/block_inspector.h"

#include <pnga/trace-model/provenance.h>

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <limits>
#include <utility>

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

QString compression_ratio_text(
    const pnga::analysis_engine::BlockInspectorRow& block) {
  if (block.input_bit_end <= block.input_bit_begin ||
      block.output_end <= block.output_begin) {
    return QStringLiteral("—");
  }
  const std::uint64_t compressed_bits =
      block.input_bit_end - block.input_bit_begin;
  const std::uint64_t output_bytes = block.output_end - block.output_begin;
  const long double ratio_percent =
      (static_cast<long double>(compressed_bits) / 8.0L) /
      static_cast<long double>(output_bytes) * 100.0L;
  return QStringLiteral("%1% (%2 bits / %3 bytes)")
      .arg(QString::number(static_cast<double>(ratio_percent), 'f', 1))
      .arg(static_cast<qulonglong>(compressed_bits))
      .arg(static_cast<qulonglong>(output_bytes));
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

BlockInspector::BlockInspector(QWidget* parent)
    : CompressionInspectorPage(parent) {
  QTableWidget* table = masterTable();
  table->setObjectName(QStringLiteral("blockInspectorTable"));
  table->setAccessibleName(QStringLiteral("Associated DEFLATE blocks"));
  table->setColumnCount(5);
  table->setHorizontalHeaderLabels(
      {QStringLiteral("#"), QStringLiteral("Type"), QStringLiteral("Final"),
       QStringLiteral("zlib bits"), QStringLiteral("Output bytes")});

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
  bounded_view_ = view;
  renderView();
}

void BlockInspector::setFastIndex(
    const pnga::analysis_engine::FastCompressionIndexView& view) {
  fast_index_ = view;
  has_fast_index_ = true;
  renderView();
}

void BlockInspector::clearFastIndex() {
  fast_index_ = pnga::analysis_engine::FastCompressionIndexView{};
  has_fast_index_ = false;
  renderView();
}

void BlockInspector::renderView() {
  view_ = bounded_view_;
  if (has_fast_index_) {
    switch (fast_index_.status) {
      case pnga::analysis_engine::FastCompressionIndexStatus::kReady:
        view_.status = pnga::analysis_engine::BlockInspectorStatus::kReady;
        break;
      case pnga::analysis_engine::FastCompressionIndexStatus::kPartial:
        view_.status = pnga::analysis_engine::BlockInspectorStatus::kPartial;
        break;
      case pnga::analysis_engine::FastCompressionIndexStatus::kError:
        view_.status = pnga::analysis_engine::BlockInspectorStatus::kError;
        break;
      case pnga::analysis_engine::FastCompressionIndexStatus::kUnavailable:
        view_.status = pnga::analysis_engine::BlockInspectorStatus::kNoTrace;
        break;
    }
    view_.generation = fast_index_.generation;
    view_.error = fast_index_.error;
    view_.rows.clear();
    view_.rows.reserve(fast_index_.blocks.size());
    for (const auto& fast_row : fast_index_.blocks) {
      pnga::analysis_engine::BlockInspectorRow row;
      row.block_index = fast_row.block_index;
      row.type = fast_row.type;
      row.last = fast_row.last;
      row.input_bit_begin = fast_row.input_range.begin.value;
      row.input_bit_end = fast_row.input_range.end.value;
      row.output_begin = fast_row.output_range.begin.value;
      row.output_end = fast_row.output_range.end.value;
      row.physical_spans = fast_row.physical_spans;
      for (const auto& bounded_row : bounded_view_.rows) {
        if (bounded_row.block_index == row.block_index) {
          row.current_output_position = bounded_row.current_output_position;
          if (row.current_output_position.has_value()) {
            view_.selected_block_index = row.block_index;
          }
          break;
        }
      }
      view_.rows.push_back(std::move(row));
    }
  }
  QTableWidget* table = masterTable();
  table->setRowCount(0);
  associated_table_row_.reset();
  std::size_t rendered = 0;
  for (const auto& row : view_.rows) {
    if (rendered++ >= static_cast<std::size_t>(kMaxVisibleRows)) {
      break;
    }
    const int table_row = table->rowCount();
    table->insertRow(table_row);
    table->setItem(table_row, 0,
                   new QTableWidgetItem(QString::number(
                       static_cast<qulonglong>(row.block_index))));
    table->setItem(table_row, 1,
                   new QTableWidgetItem(QLatin1String(
                       pnga::deflate_index::block_type_text(row.type))));
    table->setItem(table_row, 2,
                   new QTableWidgetItem(row.last ? QStringLiteral("yes")
                                                 : QStringLiteral("no")));
    table->setItem(
        table_row, 3,
        new QTableWidgetItem(QStringLiteral("%1～%2")
                                 .arg(static_cast<qulonglong>(
                                     row.input_bit_begin))
                                 .arg(static_cast<qulonglong>(
                                     row.input_bit_end))));
    table->setItem(
        table_row, 4,
        new QTableWidgetItem(QStringLiteral("%1～%2")
                                 .arg(static_cast<qulonglong>(row.output_begin))
                                 .arg(static_cast<qulonglong>(row.output_end))));
    if (row.current_output_position.has_value()) {
      markAssociatedRow(table, table_row);
      associated_table_row_ = table_row;
    }
  }
  if (view_.rows.size() > static_cast<std::size_t>(kMaxVisibleRows)) {
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("…")));
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("truncated")));
    table->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1 more rows")
                                                    .arg(static_cast<qulonglong>(
                                                        view_.rows.size() -
                                                        kMaxVisibleRows))));
  }
  updateButtons();
  updateDetails();
  // A committed pixel (trace update) selects a new associated block; keep the
  // corresponding Current row visible so it follows the click instead of
  // staying off-screen. Scrolling never changes the manual row selection.
  if (isVisible()) {
    scrollToAssociatedRow();
  }
}

void BlockInspector::scrollToAssociatedRow() {
  if (!associated_table_row_.has_value()) {
    return;
  }
  QTableWidget* table = masterTable();
  const int row = *associated_table_row_;
  QTableWidgetItem* item = row >= 0 && row < table->rowCount()
                               ? table->item(row, 0)
                               : nullptr;
  if (item != nullptr) {
    table->scrollToItem(item, QAbstractItemView::PositionAtCenter);
  }
}

void BlockInspector::showEvent(QShowEvent* event) {
  CompressionInspectorPage::showEvent(event);
  // The bundle may have updated while this page was hidden (the user clicked a
  // pixel elsewhere); reveal the associated Current row on display.
  scrollToAssociatedRow();
}

void BlockInspector::setExternalStatus(const QString& /*text*/) {
  // WP-5U12: the shared Compression context owns the trace status.
}

void BlockInspector::clear() {
  bounded_view_ = pnga::analysis_engine::BlockInspectorView{};
  clearFastIndex();
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
      QStringLiteral("zlib-stream bits %1")
          .arg(range_text(block.input_bit_begin, block.input_bit_end)));
  details.emplace_back(
      QStringLiteral("Output"),
      QStringLiteral("Inflated bytes %1")
          .arg(range_text(block.output_begin, block.output_end)));
  details.emplace_back(QStringLiteral("Compression ratio"),
                       compression_ratio_text(block));
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
    QVector<QPair<quint64, quint64>> ranges;
    ranges.reserve(static_cast<qsizetype>(spans.size()));
    for (const auto& span : spans) {
      ranges.push_back({static_cast<quint64>(span.offset),
                        static_cast<quint64>(span.length)});
    }
    emit showInHexSpansRequested(std::move(ranges));
    // Preserve the original signal for existing integrations. The
    // application-level connection uses the segmented signal above.
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
