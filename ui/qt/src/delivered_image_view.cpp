// WP-204 DeliveredImageView: zoom/pan rendering of the delivered RGBA image.

#include "pnga/ui/qt/delivered_image_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QToolButton>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace pnga::ui::qt {

DeliveredImageView::DeliveredImageView(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);

  auto* controls = new QFrame(this);
  controls->setObjectName(QStringLiteral("imageZoomControls"));
  controls->setFrameShape(QFrame::NoFrame);
  controls->setAccessibleName(QStringLiteral("Image zoom controls"));
  auto* controls_layout = new QHBoxLayout(controls);
  controls_layout->setContentsMargins(1, 1, 1, 1);
  controls_layout->setSpacing(0);

  zoom_out_button_ = new QToolButton(controls);
  zoom_out_button_->setObjectName(QStringLiteral("imageZoomOut"));
  zoom_out_button_->setText(QString::fromUtf8("−"));
  zoom_out_button_->setToolTip(QStringLiteral("Zoom out"));
  zoom_out_button_->setAccessibleName(QStringLiteral("Zoom out"));
  controls_layout->addWidget(zoom_out_button_);

  zoom_percent_combo_ = new QComboBox(controls);
  zoom_percent_combo_->setObjectName(QStringLiteral("imageZoomPercent"));
  zoom_percent_combo_->setEditable(true);
  zoom_percent_combo_->setFrame(false);
  zoom_percent_combo_->setInsertPolicy(QComboBox::NoInsert);
  zoom_percent_combo_->setMinimumContentsLength(4);
  zoom_percent_combo_->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  zoom_percent_combo_->setAccessibleName(QStringLiteral("Image zoom percentage"));
  const QStringList common_zoom_percentages = {
      QStringLiteral("25%"), QStringLiteral("50%"),
      QStringLiteral("75%"), QStringLiteral("100%"),
      QStringLiteral("125%"), QStringLiteral("150%"),
      QStringLiteral("200%")};
  zoom_percent_combo_->addItems(common_zoom_percentages);
  zoom_percent_combo_->lineEdit()->setFrame(false);
  zoom_percent_combo_->lineEdit()->setAlignment(Qt::AlignCenter);
  zoom_dropdown_indicator_ = new QLabel(zoom_percent_combo_);
  zoom_dropdown_indicator_->setObjectName(
      QStringLiteral("imageZoomDropdownIndicator"));
  zoom_dropdown_indicator_->setText(QString::fromUtf8("▾"));
  zoom_dropdown_indicator_->setAlignment(Qt::AlignCenter);
  zoom_dropdown_indicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
  controls_layout->addWidget(zoom_percent_combo_);

  zoom_in_button_ = new QToolButton(controls);
  zoom_in_button_->setObjectName(QStringLiteral("imageZoomIn"));
  zoom_in_button_->setText(QStringLiteral("+"));
  zoom_in_button_->setToolTip(QStringLiteral("Zoom in"));
  zoom_in_button_->setAccessibleName(QStringLiteral("Zoom in"));
  controls_layout->addWidget(zoom_in_button_);

  zoom_controls_ = controls;
  connect(zoom_out_button_, &QToolButton::clicked, this,
          [this] { adjustZoom(0.8); });
  connect(zoom_in_button_, &QToolButton::clicked, this,
          [this] { adjustZoom(1.25); });
  connect(zoom_percent_combo_, &QComboBox::textActivated, this,
          [this](const QString& text) { applyZoomText(text); });
  connect(zoom_percent_combo_->lineEdit(), &QLineEdit::editingFinished, this,
          [this] { applyZoomText(zoom_percent_combo_->currentText()); });

  updateZoomControls();
}

void DeliveredImageView::setImage(const QImage& image) {
  image_ = image;
  clearHoverPixel();
  clearLockedPixel();
  manual_zoom_ = false;
  refit();
  update();
}

std::optional<std::array<std::uint8_t, 4>> DeliveredImageView::rgbaAt(
    int x, int y) const {
  if (image_.isNull() || x < 0 || y < 0 || x >= image_.width() ||
      y >= image_.height()) {
    return std::nullopt;
  }
  const QRgb px = image_.pixel(x, y);
  return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(qRed(px)),
                                     static_cast<std::uint8_t>(qGreen(px)),
                                     static_cast<std::uint8_t>(qBlue(px)),
                                     static_cast<std::uint8_t>(qAlpha(px))};
}

QPoint DeliveredImageView::imagePointAt(const QPoint& widgetPoint) const {
  const auto pixel = imagePixelAt(widgetPoint);
  if (pixel.has_value()) {
    return *pixel;
  }
  const QPointF p = transform_.widgetToImage(QPointF(widgetPoint));
  return QPoint(static_cast<int>(std::floor(p.x())),
                static_cast<int>(std::floor(p.y())));
}

std::optional<QPoint> DeliveredImageView::imagePixelAt(
    const QPoint& widgetPoint) const {
  if (image_.isNull()) {
    return std::nullopt;
  }
  const QPointF origin = transform_.imageToWidget(QPointF(0, 0));
  const QSizeF scaled = image_.size() * transform_.zoom();
  const QPointF point(widgetPoint);
  if (point.x() < origin.x() || point.y() < origin.y() ||
      point.x() >= origin.x() + scaled.width() ||
      point.y() >= origin.y() + scaled.height()) {
    return std::nullopt;
  }
  const QPointF image_point = transform_.widgetToImage(point);
  const int x = static_cast<int>(std::floor(image_point.x()));
  const int y = static_cast<int>(std::floor(image_point.y()));
  if (x < 0 || y < 0 || x >= image_.width() || y >= image_.height()) {
    return std::nullopt;
  }
  return QPoint(x, y);
}

void DeliveredImageView::setHoverPixel(const QPoint& pixel) {
  if (pixel.x() < 0 || pixel.y() < 0 || pixel.x() >= image_.width() ||
      pixel.y() >= image_.height()) {
    clearHoverPixel();
    return;
  }
  hover_pixel_ = pixel;
  update();
}

void DeliveredImageView::clearHoverPixel() {
  if (hover_pixel_.has_value()) {
    hover_pixel_.reset();
    update();
  }
}

void DeliveredImageView::setLockedPixel(const QPoint& pixel) {
  if (pixel.x() < 0 || pixel.y() < 0 || pixel.x() >= image_.width() ||
      pixel.y() >= image_.height()) {
    clearLockedPixel();
    return;
  }
  locked_pixel_ = pixel;
  update();
}

void DeliveredImageView::clearLockedPixel() {
  if (locked_pixel_.has_value()) {
    locked_pixel_.reset();
    update();
  }
}

void DeliveredImageView::refit() {
  transform_.fit(size(), image_.size());
  updateZoomControls();
}

void DeliveredImageView::adjustZoom(double factor) {
  if (image_.isNull()) {
    return;
  }
  transform_.zoomBy(factor, QPointF(rect().center()));
  manual_zoom_ = true;
  updateZoomControls();
  update();
}

void DeliveredImageView::applyZoomText(const QString& text) {
  if (image_.isNull()) {
    return;
  }
  QString numeric_text = text.trimmed();
  if (numeric_text.endsWith(QLatin1Char('%'))) {
    numeric_text.chop(1);
  }
  bool ok = false;
  const double percent = numeric_text.trimmed().toDouble(&ok);
  if (!ok || !std::isfinite(percent) || percent < 1.0 || percent > 6400.0) {
    updateZoomControls();
    return;
  }
  const double current_zoom = transform_.zoom();
  if (current_zoom <= 0.0) {
    updateZoomControls();
    return;
  }
  transform_.zoomBy(percent / 100.0 / current_zoom, QPointF(rect().center()));
  manual_zoom_ = true;
  updateZoomControls();
  update();
}

void DeliveredImageView::updateZoomControls() {
  if (zoom_controls_ == nullptr) {
    return;
  }
  const bool enabled = !image_.isNull();
  zoom_controls_->setEnabled(enabled);
  QSignalBlocker blocker(zoom_percent_combo_);
  if (!enabled) {
    zoom_percent_combo_->setEditText(QStringLiteral("—"));
    return;
  }
  zoom_percent_combo_->setEditText(
      QStringLiteral("%1%")
          .arg(QString::number(zoomPercent(), 'f', 0)));
}

void DeliveredImageView::layoutZoomControls() {
  if (zoom_controls_ == nullptr) {
    return;
  }
  const QSize control_size = zoom_controls_->sizeHint();
  zoom_controls_->setGeometry(
      qMax(0, width() - control_size.width()),
      qMax(0, height() - control_size.height()),
      control_size.width(), control_size.height());
  if (zoom_dropdown_indicator_ != nullptr) {
    constexpr int kIndicatorWidth = 11;
    zoom_dropdown_indicator_->setGeometry(
        zoom_percent_combo_->width() - kIndicatorWidth, 0, kIndicatorWidth,
        zoom_percent_combo_->height());
    zoom_dropdown_indicator_->raise();
  }
  zoom_controls_->raise();
}

void DeliveredImageView::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.fillRect(rect(), palette().color(QPalette::Window));
  if (image_.isNull()) {
    p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No image"));
    return;
  }
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  const QPointF origin = transform_.imageToWidget(QPointF(0, 0));
  const QSizeF scaled = image_.size() * transform_.zoom();
  p.drawImage(QRectF(origin, scaled), image_);

  const auto drawMarker = [this, &p, origin](const QPoint& pixel,
                                              const QColor& color,
                                              Qt::PenStyle style) {
    const qreal zoom = transform_.zoom();
    const QRectF rect(origin.x() + pixel.x() * zoom,
                      origin.y() + pixel.y() * zoom, zoom, zoom);
    QPen pen(color, zoom >= 4.0 ? 2.0 : 1.0, style);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
    if (zoom < 4.0) {
      const QPointF center = rect.center();
      p.drawLine(center + QPointF(-4, 0), center + QPointF(4, 0));
      p.drawLine(center + QPointF(0, -4), center + QPointF(0, 4));
    }
  };
  if (hover_pixel_.has_value()) {
    drawMarker(*hover_pixel_, QColor(0xFF, 0xD5, 0x4F), Qt::DashLine);
  }
  if (locked_pixel_.has_value()) {
    drawMarker(*locked_pixel_, QColor(0xF4, 0x43, 0x36), Qt::SolidLine);
  }
}

void DeliveredImageView::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (!manual_zoom_ || event->oldSize().isEmpty()) {
    refit();
  } else {
    const QPointF previous_center =
        transform_.widgetToImage(QPointF(event->oldSize().width() / 2.0,
                                         event->oldSize().height() / 2.0));
    transform_.centerOn(previous_center, size());
    updateZoomControls();
  }
  layoutZoomControls();
}

void DeliveredImageView::wheelEvent(QWheelEvent* event) {
  const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
  transform_.zoomBy(factor, QPointF(event->position()));
  manual_zoom_ = true;
  updateZoomControls();
  update();
  event->accept();
}

void DeliveredImageView::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  setFocus(Qt::MouseFocusReason);
  lastMouse_ = event->pos();
  pressPosition_ = event->pos();
  panning_ = true;
  dragged_ = false;
  event->accept();
}

void DeliveredImageView::mouseMoveEvent(QMouseEvent* event) {
  const auto pixel = imagePixelAt(event->pos());
  if (pixel.has_value()) {
    if (!hover_pixel_.has_value() || *hover_pixel_ != *pixel) {
      setHoverPixel(*pixel);
      emit pixelHovered(pixel->x(), pixel->y());
    }
  } else if (hover_pixel_.has_value()) {
    clearHoverPixel();
    emit pixelHoverLeft();
  }
  if (panning_) {
    const QPoint delta = event->pos() - lastMouse_;
    lastMouse_ = event->pos();
    if ((event->pos() - pressPosition_).manhattanLength() >=
        QApplication::startDragDistance()) {
      dragged_ = true;
    }
    if (dragged_) {
      transform_.panBy(QPointF(delta));
      update();
    }
  }
  event->accept();
}

void DeliveredImageView::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && panning_) {
    panning_ = false;
    // A click without dragging publishes a pixel selection.
    if (!dragged_ && !image_.isNull()) {
      const auto pixel = imagePixelAt(event->pos());
      if (pixel.has_value()) {
        emit pixelSelected(pixel->x(), pixel->y());
      }
    }
  }
  event->accept();
}

void DeliveredImageView::leaveEvent(QEvent* event) {
  clearHoverPixel();
  emit pixelHoverLeft();
  QWidget::leaveEvent(event);
}

void DeliveredImageView::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Left:
      emit pixelNudgeRequested(-1, 0);
      event->accept();
      return;
    case Qt::Key_Right:
      emit pixelNudgeRequested(1, 0);
      event->accept();
      return;
    case Qt::Key_Up:
      emit pixelNudgeRequested(0, -1);
      event->accept();
      return;
    case Qt::Key_Down:
      emit pixelNudgeRequested(0, 1);
      event->accept();
      return;
    case Qt::Key_Escape:
      emit selectionCancelled();
      event->accept();
      return;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
}

}  // namespace pnga::ui::qt
