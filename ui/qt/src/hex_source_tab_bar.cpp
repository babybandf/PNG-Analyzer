#include "pnga/ui/qt/hex_source_tab_bar.h"

#include <QSignalBlocker>

#include <iterator>

namespace pnga::ui::qt {

namespace {

struct SourcePresentation {
  const char* label;
  const char* tooltip;
  const char* accessible;
};

constexpr SourcePresentation kSources[] = {
    {"File", "PNG physical file bytes", "Hex source: File"},
    {"IDAT", "Virtual concatenated IDAT payload stream", "Hex source: IDAT"},
    {"Inflated", "Inflate output filtered scanline bytes",
     "Hex source: Inflated"},
    {"Defiltered", "Reconstructed packed scanline bytes",
     "Hex source: Defiltered"},
};

}  // namespace

HexSourceTabBar::HexSourceTabBar(QWidget* parent) : QTabBar(parent) {
  setObjectName(QStringLiteral("hexSourceTabs"));
  setAccessibleName(QStringLiteral("Hex data source tabs"));
  setToolTip(QStringLiteral("Select the byte source shown in Hex View"));
  setShape(QTabBar::RoundedWest);
  setExpanding(false);
  setUsesScrollButtons(true);
  setElideMode(Qt::ElideNone);
  setMovable(false);
  setDocumentMode(true);
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  // RoundedWest reserves style padding on both sides of every tab. Keep the
  // bar itself content-sized so it does not consume HexView's horizontal
  // space, while retaining enough inset for rotated labels to remain legible
  // at normal and high-DPI font sizes. Keep the tab and the small gaps between
  // tabs on one neutral gray surface; the selected source is communicated by
  // bold text. Native scroll buttons handle constrained heights.
  setStyleSheet(QStringLiteral(
      "QTabBar { background-color: #d0d0d0; }"
      "QTabBar::tab { padding-left: 8px; padding-right: 8px; "
      "padding-top: 3px; padding-bottom: 3px; margin: 2px; "
      "background-color: #d0d0d0; color: #202124; }"
      "QTabBar::tab:selected { background-color: #d0d0d0; "
      "color: #202124; font-weight: bold; }"
      "QTabBar::tab:hover { background-color: #d0d0d0; }"
      "QTabBar::tab:first { margin-top: 6px; }"));

  for (const auto& presentation : kSources) {
    const int index = addTab(QString::fromLatin1(presentation.label));
    setTabData(index, index);
    setTabToolTip(index, QString::fromLatin1(presentation.tooltip));
    setTabWhatsThis(index, QString::fromLatin1(presentation.accessible));
  }
  setMinimumWidth(0);
  setCurrentIndex(0);
  connect(this, &QTabBar::currentChanged, this,
          &HexSourceTabBar::onCurrentChanged);
}

HexSource HexSourceTabBar::source() const noexcept {
  return sourceForIndex(currentIndex());
}

void HexSourceTabBar::setSource(HexSource source) {
  const QSignalBlocker blocker(this);
  setCurrentIndex(indexForSource(source));
}

void HexSourceTabBar::onCurrentChanged(int index) {
  if (index >= 0 && index < static_cast<int>(std::size(kSources))) {
    emit sourceChanged(sourceForIndex(index));
  }
}

HexSource HexSourceTabBar::sourceForIndex(int index) noexcept {
  switch (index) {
    case 1:
      return HexSource::kIdatStream;
    case 2:
      return HexSource::kInflated;
    case 3:
      return HexSource::kDefiltered;
    case 0:
    default:
      return HexSource::kFile;
  }
}

int HexSourceTabBar::indexForSource(HexSource source) noexcept {
  return static_cast<int>(source) >= 0 && static_cast<int>(source) < 4
             ? static_cast<int>(source)
             : 0;
}

}  // namespace pnga::ui::qt
