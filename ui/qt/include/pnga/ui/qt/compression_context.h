#ifndef PNGA_UI_QT_COMPRESSION_CONTEXT_H
#define PNGA_UI_QT_COMPRESSION_CONTEXT_H

// WP-5U12: the shared Compression context shown once above the page stack. It
// is a presentation-only owner fed by TraceInspectorBinding; it formats no
// trace data and never requests a replay.

#include <QString>
#include <QWidget>

class QLabel;

namespace pnga::ui::qt {

class CompressionContext final : public QWidget {
  Q_OBJECT
 public:
  explicit CompressionContext(QWidget* parent = nullptr);

  void setStatusText(const QString& text);
  void setMappingText(const QString& text);
  void clear();

  QLabel* statusLabel() const noexcept { return status_; }
  QLabel* mappingLabel() const noexcept { return mapping_; }

 private:
  QLabel* status_ = nullptr;
  QLabel* mapping_ = nullptr;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_COMPRESSION_CONTEXT_H
