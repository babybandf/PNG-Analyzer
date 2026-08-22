// WP-505A Block Inspector widget implementation.

#include "pnga/ui/qt/block_inspector.h"

#include <pnga/trace-model/provenance.h>

#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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

}  // namespace

BlockInspector::BlockInspector(QWidget* parent) : QWidget(parent) {
  status_ = new QLabel(QStringLiteral("Block trace: no trace"), this);
  status_->setObjectName(QStringLiteral("blockInspectorStatus"));
  context_ = new QLabel(QStringLiteral("scanline: — | current output: —"),
                        this);
  context_->setObjectName(QStringLiteral("blockInspectorContext"));

  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("blockInspectorTable"));
  table_->setColumnCount(8);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Block"), QStringLiteral("BTYPE"),
       QStringLiteral("BFINAL"), QStringLiteral("Input bits"),
       QStringLiteral("Output bytes"), QStringLiteral("Current"),
       QStringLiteral("IDAT spans"), QStringLiteral("Scanline")});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);

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
          &BlockInspector::updateButtons);
  connect(hex_button_, &QPushButton::clicked, this,
          &BlockInspector::showSelectedInHex);
  connect(deflate_button_, &QPushButton::clicked, this,
          &BlockInspector::showSelectedInDeflate);
}

void BlockInspector::setView(
    const pnga::analysis_engine::BlockInspectorView& view) {
  view_ = view;
  table_->setRowCount(0);
  status_->setText(QStringLiteral("Block trace: %1 (generation %2)%3")
                       .arg(QLatin1String(
                           pnga::analysis_engine::block_inspector_status_text(
                               view_.status)))
                       .arg(static_cast<qulonglong>(view_.generation))
                       .arg(view_.error.empty()
                                ? QString{}
                                : QStringLiteral(" — %1")
                                      .arg(QString::fromStdString(view_.error))));
  context_->setText(
      QStringLiteral("scanline: %1 | current output: %2")
          .arg(view_.scanline.has_value()
                   ? QString::number(static_cast<qulonglong>(*view_.scanline))
                   : QStringLiteral("—"))
          .arg(view_.selected_output_offset.has_value()
                   ? QString::number(static_cast<qulonglong>(
                         *view_.selected_output_offset))
                   : QStringLiteral("—")));
  for (const auto& row : view_.rows) {
    const int table_row = table_->rowCount();
    table_->insertRow(table_row);
    table_->setItem(table_row, 0,
                    new QTableWidgetItem(QString::number(
                        static_cast<qulonglong>(row.block_index))));
    table_->setItem(table_row, 1,
                    new QTableWidgetItem(QLatin1String(
                        pnga::deflate_index::block_type_text(row.type))));
    table_->setItem(table_row, 2,
                    new QTableWidgetItem(row.last ? QStringLiteral("1")
                                                  : QStringLiteral("0")));
    table_->setItem(
        table_row, 3,
        new QTableWidgetItem(QStringLiteral("%1..%2")
                                 .arg(static_cast<qulonglong>(
                                     row.input_bit_begin))
                                 .arg(static_cast<qulonglong>(
                                     row.input_bit_end))));
    table_->setItem(
        table_row, 4,
        new QTableWidgetItem(QStringLiteral("%1..%2")
                                 .arg(static_cast<qulonglong>(row.output_begin))
                                 .arg(static_cast<qulonglong>(row.output_end))));
    table_->setItem(
        table_row, 5,
        new QTableWidgetItem(row.current_output_position.has_value()
                                 ? QString::number(static_cast<qulonglong>(
                                       *row.current_output_position))
                                 : QStringLiteral("—")));
    table_->setItem(table_row, 6, new QTableWidgetItem(span_text(
        row.physical_spans)));
    table_->setItem(table_row, 7,
                    new QTableWidgetItem(view_.scanline.has_value()
                                             ? QString::number(static_cast<qulonglong>(
                                                   *view_.scanline))
                                             : QStringLiteral("—")));
  }
  if (view_.selected_block_index.has_value()) {
    for (int row = 0; row < table_->rowCount(); ++row) {
      if (table_->item(row, 0)->text().toULongLong() ==
          *view_.selected_block_index) {
        table_->selectRow(row);
        break;
      }
    }
  }
  updateButtons();
}

void BlockInspector::clear() {
  setView(pnga::analysis_engine::BlockInspectorView{});
}

void BlockInspector::updateButtons() {
  const auto rows = table_->selectionModel()->selectedRows();
  const bool valid = rows.size() == 1 &&
                     rows.front().row() <
                         static_cast<int>(view_.rows.size());
  hex_button_->setEnabled(valid &&
                          !view_.rows[static_cast<std::size_t>(rows.front().row())]
                               .physical_spans.empty());
  deflate_button_->setEnabled(valid);
}

void BlockInspector::showSelectedInHex() {
  const auto rows = table_->selectionModel()->selectedRows();
  if (rows.size() != 1) {
    return;
  }
  const auto& spans = view_.rows[static_cast<std::size_t>(rows.front().row())]
                          .physical_spans;
  if (!spans.empty()) {
    emit showInHexRequested(static_cast<quint64>(spans.front().offset),
                            static_cast<quint64>(spans.front().length));
  }
}

void BlockInspector::showSelectedInDeflate() {
  const auto rows = table_->selectionModel()->selectedRows();
  if (rows.size() != 1) {
    return;
  }
  const auto& block = view_.rows[static_cast<std::size_t>(rows.front().row())];
  emit showInDeflateRequested(static_cast<quint64>(block.input_bit_begin),
                              static_cast<quint64>(block.input_bit_end));
}

}  // namespace pnga::ui::qt
