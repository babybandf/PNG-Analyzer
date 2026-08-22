// WP-104 HexView: windowed hex dump. Reads only the visible lines per paint
// and renders offset / hex / ASCII columns with optional span highlights.

#include "pnga/ui/qt/hex_view.h"

#include <QColor>
#include <QFontDatabase>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace pnga::ui::qt {

namespace {

constexpr int kBytesPerLine = 16;

bool is_printable(std::byte b) {
  const unsigned char c = static_cast<unsigned char>(b);
  return c >= 0x20 && c < 0x7F;
}

}  // namespace

HexView::HexView(QWidget* parent) : QAbstractScrollArea(parent) {
  setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void HexView::setSource(std::shared_ptr<const HexDataSource> source) {
  source_ = std::move(source);
  spans_.clear();
  updateScrollbars();
  viewport()->update();
}

void HexView::setHighlight(std::vector<HexHighlightSpan> spans) {
  spans_ = std::move(spans);
  viewport()->update();
}

void HexView::clearHighlight() {
  spans_.clear();
  viewport()->update();
}

std::int64_t HexView::lineCount() const {
  if (source_ == nullptr || source_->size() == 0) {
    return 0;
  }
  return static_cast<std::int64_t>(
      (source_->size() + kBytesPerLine - 1) / kBytesPerLine);
}

void HexView::updateScrollbars() {
  const std::int64_t visible =
      std::max<std::int64_t>(1, viewport()->height() / fontMetrics().height());
  const std::int64_t total = lineCount();
  QScrollBar* sb = verticalScrollBar();
  sb->setRange(0, static_cast<int>(std::max<std::int64_t>(0, total - visible)));
  sb->setPageStep(static_cast<int>(visible));
}

void HexView::scrollContentsBy(int /*dx*/, int /*dy*/) {
  viewport()->update();
}

void HexView::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);
  updateScrollbars();
}

void HexView::paintEvent(QPaintEvent*) {
  QPainter p(viewport());
  const QFontMetrics& fm = fontMetrics();
  const int lineHeight = fm.height();
  const int cw = fm.horizontalAdvance(QLatin1Char('0'));

  if (source_ == nullptr) {
    p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No file loaded"));
    return;
  }
  if (source_->size() == 0) {
    p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Empty file"));
    return;
  }

  const int firstLine = verticalScrollBar()->value();
  const int visibleLines =
      std::max(1, viewport()->height() / lineHeight) + 1;
  const std::uint64_t startOffset =
      static_cast<std::uint64_t>(firstLine) * kBytesPerLine;
  const std::uint64_t size = source_->size();
  const std::uint64_t available =
      size > startOffset ? size - startOffset : 0;
  const std::size_t readLen = static_cast<std::size_t>(
      std::min<std::uint64_t>(
          static_cast<std::uint64_t>(visibleLines) * kBytesPerLine,
          available));

  std::vector<std::byte> buf(readLen);
  if (readLen != 0 && !source_->read(startOffset, buf.data(), readLen)) {
    std::fill(buf.begin(), buf.end(), std::byte{0});
  }

  const int xOffset = 4;
  const int xHex = xOffset + (8 + 2) * cw;
  const int xAscii = xHex + (16 * 3 + 1) * cw;
  const int yBase = fm.ascent();

  p.setPen(palette().color(QPalette::Text));
  for (int line = 0; line < visibleLines; ++line) {
    const std::uint64_t off = startOffset +
                              static_cast<std::uint64_t>(line) * kBytesPerLine;
    if (off >= size) {
      break;
    }
    const int y = line * lineHeight + yBase;

    char offbuf[16];
    std::snprintf(offbuf, sizeof(offbuf), "%08llx",
                  static_cast<unsigned long long>(off));
    p.drawText(xOffset, y, QString::fromLatin1(offbuf));

    for (int b = 0; b < kBytesPerLine; ++b) {
      const std::uint64_t byteOff = off + static_cast<std::uint64_t>(b);
      const bool inRange = byteOff < size;
      const std::byte byte = inRange ? buf[byteOff - startOffset]
                                     : std::byte{0};
      QColor highlight;
      for (const auto& span : spans_) {
        if (inRange && byteOff >= span.start &&
            byteOff < span.start + span.length) {
          highlight = span.color;
          break;
        }
      }
      const int x = xHex + b * 3 * cw;
      if (highlight.isValid()) {
        p.fillRect(QRect(x, y - fm.ascent(), 3 * cw, lineHeight),
                   QColor(highlight.red(), highlight.green(),
                          highlight.blue(), 60));
      }
      QString hex;
      if (inRange) {
        hex = QStringLiteral("%1")
                  .arg(static_cast<unsigned>(byte), 2, 16,
                       QLatin1Char('0'));
      } else {
        hex = QStringLiteral("  ");
      }
      p.drawText(x, y, hex);

      const int ax = xAscii + b * cw;
      if (highlight.isValid()) {
        p.fillRect(QRect(ax, y - fm.ascent(), cw, lineHeight),
                   QColor(highlight.red(), highlight.green(),
                          highlight.blue(), 60));
      }
      if (inRange) {
        p.drawText(ax, y,
                   QChar(is_printable(byte)
                             ? static_cast<unsigned char>(byte)
                             : '.'));
      }
    }
  }
}

}  // namespace pnga::ui::qt
