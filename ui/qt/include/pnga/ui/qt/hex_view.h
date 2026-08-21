#ifndef PNGA_UI_QT_HEX_VIEW_H
#define PNGA_UI_QT_HEX_VIEW_H

// WP-104: windowed hex dump over a borrowed ByteSource. Only the visible line
// range is read on paint; the file is never copied into a whole-file hex
// string, so large files keep the UI responsive. Supports highlighting of
// multiple physical spans (header / data / CRC) with distinct colors.

#include <pnga/io/byte_source.h>

#include <QAbstractScrollArea>

#include <cstdint>
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

  // `source` is borrowed and must outlive the view; pass nullptr to clear.
  void setSource(const pnga::io::IByteSource* source);

  // Highlights the given byte spans. Spans are clamped to the source size;
  // pass an empty vector to clear.
  void setHighlight(std::vector<HexHighlightSpan> spans);
  void clearHighlight();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void scrollContentsBy(int dx, int dy) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  std::int64_t lineCount() const;
  void updateScrollbars();

  const pnga::io::IByteSource* source_ = nullptr;
  std::vector<HexHighlightSpan> spans_;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_HEX_VIEW_H
