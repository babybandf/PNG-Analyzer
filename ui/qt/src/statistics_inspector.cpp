// WP-602G: Statistics inspector implementation. Model-backed tables only
// (no QTableWidget, no per-row widgets); the sibling-page table contract
// applies: 28 px normative rows and header, Interactive content columns with
// a normative Stretch fill column, content-derived initial widths re-fit per
// document generation only so user-adjusted widths survive same-generation
// republishes (defect 2026-09-05 precedent), and tables never drive the
// page/dock minimum width. Enter on a navigation row requests the typed
// occurrence; Escape cancels. Export stays disabled until a published row
// carries a usable (verified) section.

#include "pnga/ui/qt/statistics_inspector.h"

#include "pnga/ui/qt/statistics_table_model.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QGridLayout>

#include <QMetaType>

#include <functional>
#include <utility>

namespace pnga::ui::qt {
namespace {

constexpr int kPageCount = 4;

// Normative geometry (flow-ui §20.3), consistent with the sibling pages.
constexpr int kRowHeight = 28;

// Row-selection table keyboard contract: Enter requests the typed occurrence
// of the current row, Escape cancels the running collection.
class StatisticsTableView final : public QTableView {
 public:
  using QTableView::QTableView;

  void setOccurrenceHandler(std::function<void()> handler) {
    occurrence_handler_ = std::move(handler);
  }
  void setCancelHandler(std::function<void()> handler) {
    cancel_handler_ = std::move(handler);
  }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) &&
        occurrence_handler_ != nullptr) {
      occurrence_handler_();
      event->accept();
      return;
    }
    if (key == Qt::Key_Escape && cancel_handler_ != nullptr) {
      cancel_handler_();
      event->accept();
      return;
    }
    QTableView::keyPressEvent(event);
  }

 private:
  std::function<void()> occurrence_handler_;
  std::function<void()> cancel_handler_;
};

}  // namespace

StatisticsInspector::StatisticsInspector(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("statisticsInspector"));
  setAccessibleName(QStringLiteral("Statistics inspector"));
  qRegisterMetaType<pnga::analysis_engine::StatisticsNavigationRequest>(
      "pnga::analysis_engine::StatisticsNavigationRequest");

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 2, 4, 2);
  layout->setSpacing(2);

  // A compact two-column action grid keeps the dock's minimum width well
  // below the narrow-Inspector contract (no button-driven horizontal
  // growth); long rows scroll inside the tables instead.
  auto* actions = new QGridLayout;
  actions->setContentsMargins(0, 0, 0, 0);
  refresh_button_ = new QPushButton(QStringLiteral("Refresh"), this);
  refresh_button_->setObjectName(QStringLiteral("statisticsRefresh"));
  refresh_button_->setAccessibleName(QStringLiteral("Refresh statistics"));
  cancel_button_ = new QPushButton(QStringLiteral("Cancel"), this);
  cancel_button_->setObjectName(QStringLiteral("statisticsCancel"));
  cancel_button_->setAccessibleName(QStringLiteral("Cancel statistics"));
  export_json_button_ = new QPushButton(QStringLiteral("Export JSON"), this);
  export_json_button_->setObjectName(QStringLiteral("statisticsExportJson"));
  export_json_button_->setAccessibleName(QStringLiteral("Export statistics JSON"));
  export_csv_button_ = new QPushButton(QStringLiteral("Export CSV"), this);
  export_csv_button_->setObjectName(QStringLiteral("statisticsExportCsv"));
  export_csv_button_->setAccessibleName(QStringLiteral("Export statistics CSV"));
  occurrence_button_ =
      new QPushButton(QStringLiteral("Show occurrence"), this);
  occurrence_button_->setObjectName(QStringLiteral("statisticsShowOccurrence"));
  occurrence_button_->setAccessibleName(
      QStringLiteral("Show statistics occurrence"));
  actions->addWidget(refresh_button_, 0, 0);
  actions->addWidget(cancel_button_, 0, 1);
  actions->addWidget(export_json_button_, 1, 0);
  actions->addWidget(export_csv_button_, 1, 1);
  actions->addWidget(occurrence_button_, 2, 0);
  actions->setColumnStretch(1, 1);
  layout->addLayout(actions);

  progress_label_ = new QLabel(this);
  progress_label_->setObjectName(QStringLiteral("statisticsProgress"));
  progress_label_->setAccessibleName(QStringLiteral("Statistics progress"));
  progress_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  initial_progress_text_ = QStringLiteral(
      "Statistics are collected when this tab is opened.");
  progress_label_->setText(initial_progress_text_);
  layout->addWidget(progress_label_);

  pages_widget_ = new QTabWidget(this);
  pages_widget_->setObjectName(QStringLiteral("statisticsPages"));
  pages_widget_->setAccessibleName(QStringLiteral("Statistics pages"));
  pages_widget_->setUsesScrollButtons(true);

  static constexpr const char* kPageTitles[] = {"Overview", "Chunks",
                                                "Filters", "DEFLATE"};
  static constexpr const char* kTableNames[] = {
      "statisticsOverviewTable", "statisticsChunksTable",
      "statisticsFiltersTable", "statisticsDeflateTable"};
  static constexpr const char* kTableAccessible[] = {
      "Statistics overview table", "Statistics chunks table",
      "Statistics filters table", "Statistics deflate table"};

  for (int page = 0; page < kPageCount; ++page) {
    pages_[page].model = new StatisticsTableModel(this);
    auto* table = new StatisticsTableView(this);
    pages_[page].table = table;
    table->setObjectName(QLatin1String(kTableNames[page]));
    table->setAccessibleName(QLatin1String(kTableAccessible[page]));
    table->setModel(pages_[page].model);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setDefaultSectionSize(kRowHeight);
    table->verticalHeader()->setVisible(false);
    table->setMinimumHeight(80);
    // Never let table content drive the dock minimum width; narrow pages
    // scroll horizontally inside the viewport (WP-5U12 responsive contract).
    table->setMinimumWidth(0);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto* header = table->horizontalHeader();
    header->setStretchLastSection(true);
    header->setFixedHeight(kRowHeight);
    // Content columns stay user-adjustable (Interactive) with the normative
    // Stretch fill column, matching the block/huffman/decode-trace tables.
    header->setSectionResizeMode(StatisticsTableModel::Group,
                                 QHeaderView::Interactive);
    header->setSectionResizeMode(StatisticsTableModel::Metric,
                                 QHeaderView::Interactive);
    header->setSectionResizeMode(StatisticsTableModel::Value,
                                 QHeaderView::Stretch);
    header->setSectionResizeMode(StatisticsTableModel::Unit,
                                 QHeaderView::Interactive);
    header->setSectionResizeMode(StatisticsTableModel::Status,
                                 QHeaderView::Interactive);
    table->resizeColumnsToContents();
    // The Group column only matters on the grouped DEFLATE page.
    table->setColumnHidden(StatisticsTableModel::Group, page != 3);

    const int current_page = page;
    connect(table->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this, current_page] {
              const auto rows =
                  pages_[current_page].table->selectionModel()->selectedRows();
              if (rows.size() == 1) {
                const auto* row = pages_[current_page].model->rowAt(
                    rows.front().row());
                selected_row_ids_[current_page] =
                    row == nullptr
                        ? QString()
                        : QString::fromStdString(row->id);
              } else {
                selected_row_ids_[current_page].clear();
              }
              updateOccurrenceButton();
            });
    table->setOccurrenceHandler([this, current_page] {
      requestOccurrenceFromPage(current_page);
    });
    table->setCancelHandler([this] { emit cancelRequested(); });

    pages_widget_->addTab(table, QLatin1String(kPageTitles[page]));
  }
  layout->addWidget(pages_widget_, 1);

  connect(refresh_button_, &QPushButton::clicked, this,
          [this] { emit refreshRequested(); });
  connect(cancel_button_, &QPushButton::clicked, this,
          [this] { emit cancelRequested(); });
  connect(export_json_button_, &QPushButton::clicked, this, [this] {
    emit exportRequested(static_cast<int>(StatisticsExportFormat::kJson));
  });
  connect(export_csv_button_, &QPushButton::clicked, this, [this] {
    emit exportRequested(static_cast<int>(StatisticsExportFormat::kCsv));
  });
  connect(occurrence_button_, &QPushButton::clicked, this, [this] {
    requestOccurrenceFromPage(pages_widget_->currentIndex());
  });
  // Switching pages re-evaluates Show occurrence against that page's
  // selection.
  connect(pages_widget_, &QTabWidget::currentChanged, this,
          [this](int) { updateOccurrenceButton(); });
  export_json_button_->setEnabled(false);
  export_csv_button_->setEnabled(false);
  occurrence_button_->setEnabled(false);

  // Keyboard tab order covers every action and every page table; hidden
  // tables are skipped by Qt and the visible page's table receives focus.
  QWidget::setTabOrder(refresh_button_, cancel_button_);
  QWidget::setTabOrder(cancel_button_, export_json_button_);
  QWidget::setTabOrder(export_json_button_, export_csv_button_);
  QWidget::setTabOrder(export_csv_button_, occurrence_button_);
  QWidget::setTabOrder(occurrence_button_, pages_[0].table);
  QWidget::setTabOrder(pages_[0].table, pages_[1].table);
  QWidget::setTabOrder(pages_[1].table, pages_[2].table);
  QWidget::setTabOrder(pages_[2].table, pages_[3].table);
}

void StatisticsInspector::setView(
    std::shared_ptr<const StatisticsView> view) {
  if (view == nullptr) {
    clear();
    return;
  }
  // Per-generation refit policy (defect 2026-09-05 precedent): content
  // widths re-derive only when the published generation changes; a
  // same-generation republish preserves the user's manual column widths.
  const bool generation_changed =
      !has_view_ || view->generation != view_->generation;
  const std::vector<StatisticsRow>* page_rows[kPageCount] = {
      &view->overview, &view->chunks, &view->filters, &view->deflate};

  bool any_usable_section = false;
  for (int page = 0; page < kPageCount; ++page) {
    const QString selected_id = selected_row_ids_[page];
    auto rows = std::make_shared<const std::vector<StatisticsRow>>(*page_rows[page]);
    for (const StatisticsRow& row : *rows) {
      switch (row.state.status) {
        case pnga::statistics::SectionStatus::kUnavailable:
        case pnga::statistics::SectionStatus::kCancelled:
        case pnga::statistics::SectionStatus::kError:
          break;
        default:
          any_usable_section = true;
          break;
      }
    }
    pages_[page].model->setRows(std::move(rows));
    if (!selected_id.isEmpty()) {
      const int count = pages_[page].model->rowCount();
      for (int row = 0; row < count; ++row) {
        const auto* entry = pages_[page].model->rowAt(row);
        if (entry != nullptr && selected_id == QString::fromStdString(entry->id)) {
          pages_[page].table->selectionModel()->select(
              pages_[page].model->index(row, 0),
              QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
          break;
        }
      }
    }
  }
  view_ = std::move(view);
  has_view_ = true;
  // Export is enabled when at least one verified (usable) section exists;
  // partial output stays exportable and is labeled by the publisher.
  export_json_button_->setEnabled(any_usable_section);
  export_csv_button_->setEnabled(any_usable_section);
  updateOccurrenceButton();
  if (generation_changed) {
    refitColumns();
  }
}

void StatisticsInspector::setProgress(QString text) {
  progress_label_->setText(std::move(text));
}

void StatisticsInspector::clear() {
  view_.reset();
  has_view_ = false;
  for (int page = 0; page < kPageCount; ++page) {
    pages_[page].model->setRows(nullptr);
    selected_row_ids_[page].clear();
  }
  export_json_button_->setEnabled(false);
  export_csv_button_->setEnabled(false);
  occurrence_button_->setEnabled(false);
  progress_label_->setText(initial_progress_text_);
}

void StatisticsInspector::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    emit cancelRequested();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void StatisticsInspector::requestOccurrenceFromPage(int page) {
  if (page < 0 || page >= kPageCount) {
    return;
  }
  const auto rows = pages_[page].table->selectionModel()->selectedRows();
  if (rows.size() != 1) {
    return;
  }
  const auto* row = pages_[page].model->rowAt(rows.front().row());
  if (row == nullptr || !row->navigation.has_value()) {
    return;
  }
  emit occurrenceRequested(*row->navigation);
}

void StatisticsInspector::updateOccurrenceButton() {
  const int page = pages_widget_->currentIndex();
  bool enabled = false;
  if (page >= 0 && page < kPageCount) {
    const auto rows = pages_[page].table->selectionModel()->selectedRows();
    if (rows.size() == 1) {
      const auto* row = pages_[page].model->rowAt(rows.front().row());
      enabled = row != nullptr && row->navigation.has_value();
    }
  }
  occurrence_button_->setEnabled(enabled);
}

void StatisticsInspector::refitColumns() {
  for (int page = 0; page < kPageCount; ++page) {
    pages_[page].table->resizeColumnsToContents();
  }
}

}  // namespace pnga::ui::qt
