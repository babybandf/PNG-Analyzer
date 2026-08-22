#ifndef PNGA_UI_QT_HEX_VIEW_H
#define PNGA_UI_QT_HEX_VIEW_H

// WP-104: windowed hex dump over a borrowed ByteSource. Only the visible line
// range is read on paint; the file is never copied into a whole-file hex
// string, so large files keep the UI responsive. Supports highlighting of
// multiple physical spans (header / data / CRC) with distinct colors.

#include <pnga/ui/qt/hex_data_source.h>

#include <QAbstractScrollArea>

#include <cstdint>
#include <optional>
#include <vector>

class QColor;
class QPaintEvent;

namespace pnga::ui::qt {

struct HexHighlightSpan {
  std::uint64_t start = 0;
  std::uint64_t length = 0;
  QColor color;
};

class HexView final : public QAbstractScrollArea {
  Q_OBJECT
 public:
  explicit HexView(QWidget* parent = nullptr);

  // The source owns or otherwise keeps its backing bytes alive.
  void setSource(std::shared_ptr<const HexDataSource> source);

  // Highlights the given byte spans. Spans are clamped to the source size;
  // pass an empty vector to clear.
  void setHighlight(std::vector<HexHighlightSpan> spans);
  void clearHighlight();

  bool navigateTo(std::uint64_t offset);
  bool goBack();
  bool goForward();
  std::optional<std::uint64_t> currentLocation() const noexcept;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void resizeEvent(QResizeEvent* event) override;

 signals:
  void locationChanged(std::uint64_t offset);

 private:
  std::int64_t lineCount() const;
  void updateScrollbars();

  std::shared_ptr<const HexDataSource> source_;
  std::vector<HexHighlightSpan> spans_;
  std::vector<std::uint64_t> history_;
  std::size_t history_index_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_HEX_VIEW_H
