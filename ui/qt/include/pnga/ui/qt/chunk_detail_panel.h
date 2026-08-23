#ifndef PNGA_UI_QT_CHUNK_DETAIL_PANEL_H
#define PNGA_UI_QT_CHUNK_DETAIL_PANEL_H

// WP-5U8: Qt presentation for the Qt-free ChunkDetail view.

#include <pnga/png-format/chunk_detail.h>

#include <QWidget>

class QLabel;
class QTableWidget;

namespace pnga::ui::qt {

class ChunkDetailPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit ChunkDetailPanel(QWidget* parent = nullptr);

  void setLoading();
  void setDetail(const pnga::png_format::ChunkDetail& detail);
  void clear();

  QTableWidget* table() const noexcept { return table_; }

 private:
  QLabel* summary_ = nullptr;
  QTableWidget* table_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_CHUNK_DETAIL_PANEL_H
