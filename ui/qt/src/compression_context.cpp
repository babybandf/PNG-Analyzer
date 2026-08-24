// WP-5U12 shared Compression context widget.

#include "pnga/ui/qt/compression_context.h"

#include <QLabel>
#include <QVBoxLayout>

namespace pnga::ui::qt {

CompressionContext::CompressionContext(QWidget* parent) : QWidget(parent) {
  status_ = new QLabel(QStringLiteral("Open a PNG to inspect its compressed "
                                      "IDAT stream."),
                       this);
  status_->setObjectName(QStringLiteral("compressionContextStatus"));
  status_->setWordWrap(true);
  status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  mapping_ = new QLabel(this);
  mapping_->setObjectName(QStringLiteral("compressionContextMapping"));
  mapping_->setWordWrap(true);
  mapping_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("compressionContextStreamSummary"));
  summary_->setWordWrap(true);
  summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  summary_->setVisible(false);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(2);
  layout->addWidget(status_);
  layout->addWidget(mapping_);
  layout->addWidget(summary_);
}

void CompressionContext::setStatusText(const QString& text) {
  status_->setText(text);
}

void CompressionContext::setMappingText(const QString& text) {
  mapping_->setText(text);
}

void CompressionContext::setStreamSummary(const QString& text) {
  summary_->setText(text);
  summary_->setVisible(!text.isEmpty());
}

void CompressionContext::clear() {
  setStatusText(QStringLiteral("Open a PNG to inspect its compressed IDAT "
                               "stream."));
  setMappingText(QString());
  setStreamSummary(QString());
}

}  // namespace pnga::ui::qt
