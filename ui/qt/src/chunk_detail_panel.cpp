// WP-5U8: Chunk Detail table presentation.

#include "pnga/ui/qt/chunk_detail_panel.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace pnga::ui::qt {

namespace {

QString chunk_purpose(const QString& type) {
  if (type == QStringLiteral("IHDR")) {
    return QStringLiteral("image header that defines dimensions and encoding");
  }
  if (type == QStringLiteral("PLTE")) {
    return QStringLiteral("palette entries used to map indexes to RGB colors");
  }
  if (type == QStringLiteral("IDAT")) {
    return QStringLiteral("compressed image data stream");
  }
  if (type == QStringLiteral("IEND")) {
    return QStringLiteral("end marker for the PNG datastream");
  }
  if (type == QStringLiteral("tRNS")) {
    return QStringLiteral("transparency information for palette or grayscale data");
  }
  if (type == QStringLiteral("cHRM")) {
    return QStringLiteral("primary chromaticities and white point");
  }
  if (type == QStringLiteral("gAMA")) {
    return QStringLiteral("image gamma value for display compensation");
  }
  if (type == QStringLiteral("sRGB")) {
    return QStringLiteral("sRGB rendering intent");
  }
  if (type == QStringLiteral("pHYs")) {
    return QStringLiteral("physical pixel dimensions and units");
  }
  if (type == QStringLiteral("tIME")) {
    return QStringLiteral("last image modification time");
  }
  if (type == QStringLiteral("tEXt")) {
    return QStringLiteral("uncompressed textual metadata");
  }
  if (type == QStringLiteral("zTXt")) {
    return QStringLiteral("compressed textual metadata");
  }
  if (type == QStringLiteral("iTXt")) {
    return QStringLiteral("international textual metadata with language support");
  }
  return QStringLiteral("application-specific or unsupported chunk data");
}

QString chunk_description(const QString& type) {
  const QString escaped_type = type.toHtmlEscaped();
  return QStringLiteral("<b>%1</b> — %2.")
      .arg(escaped_type, chunk_purpose(type));
}

}  // namespace

ChunkDetailPanel::ChunkDetailPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("chunkDetailPanel"));
  setAccessibleName(QStringLiteral("Selected chunk details"));
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);

  description_ = new QLabel(this);
  description_->setObjectName(QStringLiteral("chunkDetailDescription"));
  description_->setAccessibleName(QStringLiteral("Chunk purpose"));
  description_->setTextFormat(Qt::RichText);
  description_->setText(QStringLiteral("Select a chunk to see its purpose."));
  description_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  description_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                         Qt::TextSelectableByKeyboard);
  layout->addWidget(description_);

  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("chunkDetailSummary"));
  summary_->setAccessibleName(QStringLiteral("Selected chunk summary"));
  summary_->setText(QStringLiteral("Select a chunk to inspect its fields"));
  summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  summary_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                    Qt::TextSelectableByKeyboard);
  layout->addWidget(summary_);

  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("chunkDetailTable"));
  table_->setAccessibleName(QStringLiteral("Selected chunk fields"));
  table_->setColumnCount(2);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("Element"), QStringLiteral("Value")});
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setWordWrap(false);
  table_->setTextElideMode(Qt::ElideNone);
  table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  table_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  table_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->verticalHeader()->setVisible(false);
  layout->addWidget(table_, 1);
}

void ChunkDetailPanel::setLoading() {
  description_->setText(QStringLiteral("Loading the selected chunk purpose…"));
  summary_->setText(QStringLiteral("Loading selected chunk details…"));
  table_->setRowCount(0);
}

void ChunkDetailPanel::setDetail(
    const pnga::png_format::ChunkDetail& detail) {
  const QString type = QString::fromStdString(detail.type);
  description_->setText(chunk_description(type));
  summary_->setText(
      QStringLiteral("%1  •  %2 bytes  •  data @ %3")
          .arg(type)
          .arg(static_cast<qulonglong>(detail.data_length))
          .arg(static_cast<qulonglong>(detail.data_offset)));
  table_->setUpdatesEnabled(false);
  table_->setRowCount(static_cast<int>(detail.fields.size()));
  for (int row = 0; row < table_->rowCount(); ++row) {
    const auto& field = detail.fields[static_cast<std::size_t>(row)];
    auto* name = new QTableWidgetItem(QString::fromStdString(field.name));
    auto* value = new QTableWidgetItem(QString::fromStdString(field.value));
    name->setFlags(name->flags() & ~Qt::ItemIsEditable);
    value->setFlags(value->flags() & ~Qt::ItemIsEditable);
    value->setToolTip(value->text());
    table_->setItem(row, 0, name);
    table_->setItem(row, 1, value);
  }
  table_->resizeRowsToContents();
  table_->setUpdatesEnabled(true);
}

void ChunkDetailPanel::clear() {
  description_->setText(QStringLiteral("Select a chunk to see its purpose."));
  summary_->setText(QStringLiteral("Select a chunk to inspect its fields"));
  table_->setRowCount(0);
}

}  // namespace pnga::ui::qt
