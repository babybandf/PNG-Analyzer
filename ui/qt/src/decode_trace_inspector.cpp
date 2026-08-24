// WP-505C / WP-5U12 Decode Trace widget implementation.

#include "pnga/ui/qt/decode_trace_inspector.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace pnga::ui::qt {

namespace {

QString sources_text(
    const std::vector<pnga::deflate_trace::TokenOutputRange>& ranges) {
  if (ranges.empty()) {
    return QStringLiteral("—");
  }
  QString out;
  for (const auto& range : ranges) {
    if (!out.isEmpty()) {
      out += QStringLiteral(", ");
    }
    out += QStringLiteral("[%1..%2) token %3")
               .arg(static_cast<qulonglong>(range.begin))
               .arg(static_cast<qulonglong>(range.end))
               .arg(static_cast<qulonglong>(range.token_index));
  }
  return out;
}

QString range_text(std::uint64_t begin, std::uint64_t end) {
  return QStringLiteral("[%1, %2)")
      .arg(static_cast<qulonglong>(begin))
      .arg(static_cast<qulonglong>(end));
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

DecodeTraceInspector::DecodeTraceInspector(QWidget* parent)
    : CompressionInspectorPage(parent) {
  QTableWidget* table = masterTable();
  table->setObjectName(QStringLiteral("decodeTraceInspectorTable"));
  table->setAccessibleName(QStringLiteral("Decode trace tokens"));
  table->setColumnCount(4);
  table->setHorizontalHeaderLabels(
       {QStringLiteral("Token"), QStringLiteral("Path"),
       QStringLiteral("Deflate bits"),
       QStringLiteral("Output bytes")});

  hex_button_ = new QPushButton(QStringLiteral("Show in Hex"), this);
  hex_button_->setObjectName(QStringLiteral("decodeShowInHex"));
  deflate_button_ =
      new QPushButton(QStringLiteral("Show in DEFLATE"), this);
  deflate_button_->setObjectName(QStringLiteral("decodeShowInDeflate"));
  hex_button_->setEnabled(false);
  deflate_button_->setEnabled(false);
  auto* buttons = new QHBoxLayout;
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(hex_button_);
  buttons->addWidget(deflate_button_);
  buttons->addStretch(1);
  layout()->addItem(buttons);

  connect(table, &QTableWidget::itemSelectionChanged, this,
          &DecodeTraceInspector::onSelectionChanged);
  connect(hex_button_, &QPushButton::clicked, this,
          &DecodeTraceInspector::showSelectedInHex);
  connect(deflate_button_, &QPushButton::clicked, this,
          &DecodeTraceInspector::showSelectedInDeflate);
}

void DecodeTraceInspector::setView(
    const pnga::analysis_engine::DecodeTraceInspectorView& view) {
  view_ = view;
  QTableWidget* table = masterTable();
  table->setRowCount(0);
  std::size_t rendered = 0;
  for (const auto& step : view_.steps) {
    if (rendered++ >= static_cast<std::size_t>(kMaxVisibleRows)) {
      break;
    }
    const int table_row = table->rowCount();
    table->insertRow(table_row);
    table->setItem(table_row, 0, new QTableWidgetItem(QString::number(
                                     static_cast<qulonglong>(step.token_index))));
    table->setItem(table_row, 1,
                   new QTableWidgetItem(QLatin1String(
                       pnga::analysis_engine::decode_trace_path_text(step.path))));
    table->setItem(table_row, 2,
                   new QTableWidgetItem(QStringLiteral("%1..%2")
                                            .arg(static_cast<qulonglong>(
                                                step.input_bit_begin))
                                            .arg(static_cast<qulonglong>(
                                                step.input_bit_end))));
    table->setItem(table_row, 3,
                   new QTableWidgetItem(QStringLiteral("%1..%2")
                                            .arg(static_cast<qulonglong>(
                                                step.output_begin))
                                            .arg(static_cast<qulonglong>(
                                                step.output_end))));
    if (step.selected) {
      markAssociatedRow(table, table_row);
    }
  }
  if (view_.steps.size() > static_cast<std::size_t>(kMaxVisibleRows)) {
    const int row = table->rowCount();
    table->insertRow(row);
    table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("…")));
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("truncated")));
    table->setItem(row, 3, new QTableWidgetItem(QStringLiteral("more steps")));
  }
  updateButtons();
  updateDetails();
}

void DecodeTraceInspector::setExternalStatus(const QString& /*text*/) {
  // WP-5U12: the shared Compression context owns the trace status.
}

void DecodeTraceInspector::clear() {
  setView(pnga::analysis_engine::DecodeTraceInspectorView{});
}

std::optional<std::size_t> DecodeTraceInspector::activeStep() const noexcept {
  const auto rows = masterTable()->selectionModel()->selectedRows();
  if (rows.size() == 1 &&
      rows.front().row() < static_cast<int>(view_.steps.size())) {
    return static_cast<std::size_t>(rows.front().row());
  }
  for (std::size_t i = 0; i < view_.steps.size(); ++i) {
    if (view_.steps[i].selected) {
      return i;
    }
  }
  return std::nullopt;
}

void DecodeTraceInspector::onSelectionChanged() {
  updateButtons();
  updateDetails();
}

void DecodeTraceInspector::updateButtons() {
  const bool valid = activeStep().has_value();
  hex_button_->setEnabled(valid);
  deflate_button_->setEnabled(valid);
}

void DecodeTraceInspector::updateDetails() {
  if (view_.steps.empty()) {
    if (view_.status == pnga::analysis_engine::DecodeTraceInspectorStatus::kError &&
        !view_.error.empty()) {
      setDetailsInstruction(QStringLiteral("Trace stopped: %1")
                                .arg(QString::fromStdString(view_.error)));
    } else if (view_.status ==
               pnga::analysis_engine::DecodeTraceInspectorStatus::kPartial) {
      setDetailsInstruction(QStringLiteral(
          "Partial trace · verified tokens are shown above."));
    } else {
      setDetailsInstruction(QStringLiteral(
          "No tokens in the bounded result for the selected output range."));
    }
    return;
  }
  const std::optional<std::size_t> step_index = activeStep();
  if (!step_index.has_value()) {
    setDetailsInstruction(QStringLiteral(
        "Select a token to inspect its decode path, or lock a pixel to find "
        "the current token."));
    return;
  }
  const auto& step = view_.steps[*step_index];
  const QString path =
      QString::fromLatin1(pnga::analysis_engine::decode_trace_path_text(
          step.path));
  std::vector<std::pair<QString, QString>> details;
  switch (step.path) {
    case pnga::analysis_engine::DecodeTracePath::kLiteral:
      details.emplace_back(
          QStringLiteral("Huffman"),
          step.huffman_symbol.has_value()
              ? QStringLiteral("symbol %1").arg(*step.huffman_symbol)
              : QStringLiteral("—"));
      details.emplace_back(QStringLiteral("Literal"),
                           QStringLiteral("0x%1")
                               .arg(step.literal, 2, 16, QLatin1Char('0')));
      details.emplace_back(QStringLiteral("Input"),
                           QStringLiteral("DEFLATE bits %1")
                               .arg(range_text(step.input_bit_begin,
                                               step.input_bit_end)));
      details.emplace_back(QStringLiteral("Output"),
                           QStringLiteral("Inflated bytes %1")
                               .arg(range_text(step.output_begin,
                                               step.output_end)));
      if (step.selected_output_byte.has_value()) {
        details.emplace_back(
            QStringLiteral("Current"),
            QStringLiteral("output byte %1")
                .arg(static_cast<qulonglong>(*step.selected_output_byte)));
      }
      break;
    case pnga::analysis_engine::DecodeTracePath::kMatch:
      details.emplace_back(
          QStringLiteral("Huffman"),
          step.huffman_symbol.has_value()
              ? QStringLiteral("symbol %1").arg(*step.huffman_symbol)
              : QStringLiteral("—"));
      details.emplace_back(
          QStringLiteral("Length"),
          QStringLiteral("%1 = base %2 + extra %3 (%4 bits)")
              .arg(step.length)
              .arg(step.length_base)
              .arg(step.length_extra_value)
              .arg(step.length_extra_bits));
      details.emplace_back(
          QStringLiteral("Distance"),
          QStringLiteral("%1 = base %2 + extra %3 (%4 bits)")
              .arg(step.distance)
              .arg(step.distance_base)
              .arg(step.distance_extra_value)
              .arg(step.distance_extra_bits));
      details.emplace_back(QStringLiteral("Input"),
                           QStringLiteral("Deflate bits %1")
                               .arg(range_text(step.input_bit_begin,
                                               step.input_bit_end)));
      details.emplace_back(QStringLiteral("Source"),
                           sources_text(step.match_source_ranges));
      details.emplace_back(QStringLiteral("Output"),
                           QStringLiteral("Inflated bytes %1")
                               .arg(range_text(step.output_begin,
                                               step.output_end)));
      if (step.selected_output_byte.has_value()) {
        details.emplace_back(
            QStringLiteral("Current"),
            QStringLiteral("output byte %1")
                .arg(static_cast<qulonglong>(*step.selected_output_byte)));
      }
      break;
    case pnga::analysis_engine::DecodeTracePath::kEndOfBlock:
      details.emplace_back(QStringLiteral("Explanation"),
                           QStringLiteral("End of block"));
      details.emplace_back(QStringLiteral("Input"),
                           QStringLiteral("DEFLATE bits %1")
                               .arg(range_text(step.input_bit_begin,
                                               step.input_bit_end)));
      details.emplace_back(QStringLiteral("Output"),
                           QStringLiteral("Inflated bytes %1")
                               .arg(range_text(step.output_begin,
                                               step.output_end)));
      break;
  }
  setDetails(
      QStringLiteral("Token #%1 · %2")
          .arg(static_cast<qulonglong>(step.token_index))
          .arg(path),
      details);
}

void DecodeTraceInspector::showSelectedInHex() {
  const std::optional<std::size_t> step_index = activeStep();
  if (!step_index.has_value()) {
    return;
  }
  const auto& step = view_.steps[*step_index];
  emit showInHexRequested(static_cast<quint64>(step.output_begin),
                          static_cast<quint64>(step.output_end));
}

void DecodeTraceInspector::showSelectedInDeflate() {
  const std::optional<std::size_t> step_index = activeStep();
  if (!step_index.has_value()) {
    return;
  }
  const auto& step = view_.steps[*step_index];
  emit showInDeflateRequested(static_cast<quint64>(step.input_bit_begin),
                              static_cast<quint64>(step.input_bit_end));
}

}  // namespace pnga::ui::qt
