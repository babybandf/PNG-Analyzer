// WP-5U12 Compression master/detail page shell.

#include "pnga/ui/qt/compression_inspector_page.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pnga::ui::qt {

namespace {

void destroy_layout_items(QLayout* layout) {
  while (QLayoutItem* item = layout->takeAt(0)) {
    if (QLayout* child_layout = item->layout()) {
      destroy_layout_items(child_layout);
    }
    if (QWidget* widget = item->widget()) {
      delete widget;
    }
    delete item;
  }
}

}  // namespace

CompressionInspectorPage::CompressionInspectorPage(QWidget* parent)
    : QWidget(parent) {
  table_ = new QTableWidget(this);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->setMinimumHeight(80);
  // Never let table content drive the page/dock minimum width; narrow pages
  // scroll horizontally inside the viewport (WP-5U12 responsive contract).
  table_->setMinimumWidth(0);
  table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  table_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

  details_title_ = new QLabel(this);
  details_title_->setObjectName(QStringLiteral("compressionDetailsTitle"));
  details_title_->setWordWrap(true);
  details_title_->setTextFormat(Qt::RichText);

  details_body_ = new QWidget(this);
  details_body_->setObjectName(QStringLiteral("compressionDetailsBody"));
  auto* body_layout = new QVBoxLayout(details_body_);
  body_layout->setContentsMargins(4, 2, 4, 2);
  body_layout->setSpacing(3);

  auto* details_scroll = new QScrollArea(this);
  details_scroll->setObjectName(QStringLiteral("compressionDetailsScroll"));
  details_scroll->setFrameShape(QFrame::NoFrame);
  details_scroll->setWidgetResizable(true);
  details_scroll->setWidget(details_body_);

  auto* details_layout = new QVBoxLayout;
  details_layout->setContentsMargins(4, 2, 4, 2);
  details_layout->setSpacing(2);
  details_layout->addWidget(details_title_);
  details_layout->addWidget(details_scroll, 1);
  auto* details_host = new QWidget(this);
  details_host->setObjectName(QStringLiteral("compressionDetails"));
  details_host->setLayout(details_layout);

  splitter_ = new QSplitter(Qt::Vertical, this);
  splitter_->setObjectName(QStringLiteral("compressionPageSplitter"));
  splitter_->setChildrenCollapsible(false);
  splitter_->addWidget(table_);
  splitter_->addWidget(details_host);
  splitter_->setStretchFactor(0, 55);
  splitter_->setStretchFactor(1, 45);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 2, 4, 2);
  layout->addWidget(splitter_, 1);
}

void CompressionInspectorPage::clearDetailsBody() {
  if (details_body_ == nullptr) {
    return;
  }
  // Detail rows are rebuilt synchronously, often more than once before the
  // event loop returns. Nested QGridLayouts own the layout items but not the
  // QLabel children, so remove the full nested tree before adding new rows.
  // Otherwise orphaned labels remain visible at the top-left of the pane.
  destroy_layout_items(details_body_->layout());
}

void CompressionInspectorPage::clearDetails() {
  clearDetailsBody();
  details_title_->setText(QString());
}

void CompressionInspectorPage::setDetails(
    const QString& title,
    const std::vector<std::pair<QString, QString>>& rows) {
  clearDetails();
  details_title_->setText(title);
  if (rows.empty()) {
    return;
  }
  auto* grid = new QGridLayout;
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(3);
  grid->setColumnStretch(1, 1);
  for (std::size_t i = 0; i < rows.size(); ++i) {
    auto* label = new QLabel(rows[i].first, details_body_);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    auto* value = new QLabel(rows[i].second, details_body_);
    value->setTextFormat(Qt::PlainText);
    value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(label, static_cast<int>(i), 0);
    grid->addWidget(value, static_cast<int>(i), 1);
  }
  // This is a nested layout, not a generic layout item.  Adding it through
  // QBoxLayout keeps its row geometry under the body layout's control.
  static_cast<QBoxLayout*>(details_body_->layout())->addLayout(grid);
}

void CompressionInspectorPage::setDetailsInstruction(const QString& text) {
  setDetails(QString(), {{QStringLiteral("Info"), text}});
}

}  // namespace pnga::ui::qt
