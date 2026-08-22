// WP-505C Decode Trace widget implementation.

#include "pnga/ui/qt/decode_trace_inspector.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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

}  // namespace

DecodeTraceInspector::DecodeTraceInspector(QWidget* parent) : QWidget(parent) {
  status_ = new QLabel(QStringLiteral("Decode trace: no trace"), this);
  status_->setObjectName(QStringLiteral("decodeTraceInspectorStatus"));
  context_ = new QLabel(QStringLiteral("selected token: — | output: —"), this);
  context_->setObjectName(QStringLiteral("decodeTraceInspectorContext"));
  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("decodeTraceInspectorTable"));
  table_->setColumnCount(9);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Token"), QStringLiteral("Path"),
       QStringLiteral("Input bits"), QStringLiteral("Output bytes"),
       QStringLiteral("Huffman"), QStringLiteral("Length"),
       QStringLiteral("Distance"), QStringLiteral("Match source"),
       QStringLiteral("Selected")});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setStretchLastSection(true);

  hex_button_ = new QPushButton(QStringLiteral("Show in Hex"), this);
  deflate_button_ = new QPushButton(QStringLiteral("Show in DEFLATE"), this);
  hex_button_->setEnabled(false);
  deflate_button_->setEnabled(false);
  auto* buttons = new QHBoxLayout;
  buttons->addWidget(hex_button_);
  buttons->addWidget(deflate_button_);
  buttons->addStretch(1);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(status_);
  layout->addWidget(context_);
  layout->addWidget(table_, 1);
  layout->addLayout(buttons);
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &DecodeTraceInspector::updateButtons);
  connect(hex_button_, &QPushButton::clicked, this,
          &DecodeTraceInspector::showSelectedInHex);
  connect(deflate_button_, &QPushButton::clicked, this,
          &DecodeTraceInspector::showSelectedInDeflate);
}

void DecodeTraceInspector::setView(
    const pnga::analysis_engine::DecodeTraceInspectorView& view) {
  view_ = view;
  table_->setRowCount(0);
  status_->setText(QStringLiteral("Decode trace: %1 (generation %2)%3")
                       .arg(QLatin1String(
                           pnga::analysis_engine::
                               decode_trace_inspector_status_text(view_.status)))
                       .arg(static_cast<qulonglong>(view_.generation))
                       .arg(view_.error.empty()
                                ? QString{}
                                : QStringLiteral(" — %1")
                                      .arg(QString::fromStdString(view_.error))));
  context_->setText(
      QStringLiteral("selected token: %1 | output: %2")
          .arg(view_.selected_token_index.has_value()
                   ? QString::number(static_cast<qulonglong>(
                         *view_.selected_token_index))
                   : QStringLiteral("—"))
          .arg(view_.selected_output_offset.has_value()
                   ? QString::number(static_cast<qulonglong>(
                         *view_.selected_output_offset))
                   : QStringLiteral("—")));
  for (const auto& step : view_.steps) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    table_->setItem(row, 0, new QTableWidgetItem(QString::number(
        static_cast<qulonglong>(step.token_index))));
    table_->setItem(row, 1, new QTableWidgetItem(QLatin1String(
        pnga::analysis_engine::decode_trace_path_text(step.path))));
    table_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("%1..%2")
                                                     .arg(static_cast<qulonglong>(
                                                         step.input_bit_begin))
                                                     .arg(static_cast<qulonglong>(
                                                         step.input_bit_end))));
    table_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("%1..%2")
                                                     .arg(static_cast<qulonglong>(
                                                         step.output_begin))
                                                     .arg(static_cast<qulonglong>(
                                                         step.output_end))));
    table_->setItem(row, 4, new QTableWidgetItem(
                                step.huffman_symbol.has_value()
                                    ? QString::number(*step.huffman_symbol)
                                    : QStringLiteral("—")));
    table_->setItem(row, 5,
                    new QTableWidgetItem(step.path ==
                                                 pnga::analysis_engine::DecodeTracePath::kMatch
                                             ? QStringLiteral("%1 = %2 + %3 (%4 bits)")
                                                   .arg(step.length)
                                                   .arg(step.length_base)
                                                   .arg(step.length_extra_value)
                                                   .arg(step.length_extra_bits)
                                             : QStringLiteral("—")));
    table_->setItem(row, 6,
                    new QTableWidgetItem(step.path ==
                                                 pnga::analysis_engine::DecodeTracePath::kMatch
                                             ? QStringLiteral("%1 = %2 + %3 (%4 bits)")
                                                   .arg(step.distance)
                                                   .arg(step.distance_base)
                                                   .arg(step.distance_extra_value)
                                                   .arg(step.distance_extra_bits)
                                             : QStringLiteral("—")));
    table_->setItem(row, 7,
                    new QTableWidgetItem(sources_text(step.match_source_ranges)));
    table_->setItem(row, 8, new QTableWidgetItem(
                                step.selected_output_byte.has_value()
                                    ? QStringLiteral("output byte %1")
                                          .arg(static_cast<qulonglong>(
                                              *step.selected_output_byte))
                                    : (step.selected ? QStringLiteral("selected")
                                                     : QStringLiteral("—"))));
    if (step.selected) {
      table_->selectRow(row);
    }
  }
  updateButtons();
}

void DecodeTraceInspector::setExternalStatus(const QString& text) {
  status_->setText(text);
}

void DecodeTraceInspector::clear() {
  setView(pnga::analysis_engine::DecodeTraceInspectorView{});
}

void DecodeTraceInspector::updateButtons() {
  const auto rows = table_->selectionModel()->selectedRows();
  const bool valid = rows.size() == 1 &&
                     rows.front().row() <
                         static_cast<int>(view_.steps.size());
  hex_button_->setEnabled(valid);
  deflate_button_->setEnabled(valid);
}

void DecodeTraceInspector::showSelectedInHex() {
  const auto rows = table_->selectionModel()->selectedRows();
  if (rows.size() != 1) {
    return;
  }
  const auto& step = view_.steps[static_cast<std::size_t>(rows.front().row())];
  emit showInHexRequested(static_cast<quint64>(step.output_begin),
                          static_cast<quint64>(step.output_end));
}

void DecodeTraceInspector::showSelectedInDeflate() {
  const auto rows = table_->selectionModel()->selectedRows();
  if (rows.size() != 1) {
    return;
  }
  const auto& step = view_.steps[static_cast<std::size_t>(rows.front().row())];
  emit showInDeflateRequested(static_cast<quint64>(step.input_bit_begin),
                              static_cast<quint64>(step.input_bit_end));
}

}  // namespace pnga::ui::qt
