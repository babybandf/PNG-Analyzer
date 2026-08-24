#ifndef PNGA_UI_QT_COMPRESSION_INSPECTOR_PAGE_H
#define PNGA_UI_QT_COMPRESSION_INSPECTOR_PAGE_H

// WP-5U12: shared master/detail presentation shell for the three Compression
// pages. The shell owns the bounded master table, the details area and the
// splitter between them; a page owns its columns, row content and actions. It
// contains no PNG/Deflate logic and formats immutable projection rows only.

#include <QString>
#include <QWidget>

#include <utility>
#include <vector>

class QLabel;
class QSplitter;
class QTableWidget;

namespace pnga::ui::qt {

class CompressionInspectorPage : public QWidget {
  Q_OBJECT
 public:
  explicit CompressionInspectorPage(QWidget* parent = nullptr);

  void clearDetails();
  void setDetails(const QString& title,
                  const std::vector<std::pair<QString, QString>>& rows);
  void setDetailsInstruction(const QString& text);

 protected:
  QTableWidget* masterTable() const noexcept { return table_; }
  QLabel* detailsTitle() const noexcept { return details_title_; }

 private:
  void clearDetailsBody();

  QTableWidget* table_ = nullptr;
  QSplitter* splitter_ = nullptr;
  QLabel* details_title_ = nullptr;
  QWidget* details_body_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_COMPRESSION_INSPECTOR_PAGE_H
