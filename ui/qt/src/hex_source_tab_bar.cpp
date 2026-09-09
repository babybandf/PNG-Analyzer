#include "pnga/ui/qt/hex_source_tab_bar.h"

#include <QSignalBlocker>

#include <iterator>

namespace pnga::ui::qt {

namespace {

struct SourcePresentation {
  const char* label;
  const char* tooltip;
  const char* accessible;
  HexSource source;  // identity of the tab; mapping is table-driven so no
                     // consumer may locate tabs by hardcoded indexes.
};

constexpr SourcePresentation kSources[] = {
    {"File", "PNG physical file bytes", "Hex source: File",
     HexSource::kFile},
    {"IDAT", "Virtual concatenated IDAT payload stream", "Hex source: IDAT",
     HexSource::kIdatStream},
    {"Inflated", "Inflate output filtered scanline bytes",
     "Hex source: Inflated", HexSource::kInflated},
    {"Unfiltered", "Reconstructed packed scanline bytes",
     "Hex source: Unfiltered", HexSource::kDefiltered},
};

constexpr HexSource kStreamSource = HexSource::kIdatStream;

int index_for_source(HexSource source) noexcept {
  for (std::size_t i = 0; i < std::size(kSources); ++i) {
    if (kSources[i].source == source) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

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
  // Presentation is owned by the application theme. Keeping this widget free
  // of a local stylesheet lets Light/Dark modes update it atomically.

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
  if (animation_mode_ &&
      kSources[currentIndex()].source == kStreamSource) {
    return HexSource::kFrameStream;
  }
  return sourceForIndex(currentIndex());
}

void HexSourceTabBar::setSource(HexSource source) {
  const QSignalBlocker blocker(this);
  setCurrentIndex(indexForSource(source));
}

void HexSourceTabBar::setAnimationMode(bool animation) {
  const QSignalBlocker blocker(this);
  animation_mode_ = animation;
  const int stream_index = index_for_source(kStreamSource);
  const SourcePresentation& presentation =
      animation ? SourcePresentation{
                      "Frame Stream",
                      "Virtual compressed payload for the selected frame",
                      "Hex source: Frame Stream", HexSource::kIdatStream}
                : kSources[stream_index];
  setTabText(stream_index, QString::fromLatin1(presentation.label));
  setTabToolTip(stream_index, QString::fromLatin1(presentation.tooltip));
  setTabWhatsThis(stream_index,
                  QString::fromLatin1(presentation.accessible));
}

void HexSourceTabBar::onCurrentChanged(int index) {
  if (index >= 0 && index < static_cast<int>(std::size(kSources))) {
    emit sourceChanged(sourceForIndex(index));
  }
}

HexSource HexSourceTabBar::sourceForIndex(int index) noexcept {
  if (index < 0 || index >= static_cast<int>(std::size(kSources))) {
    return HexSource::kFile;
  }
  return kSources[index].source;
}

int HexSourceTabBar::indexForSource(HexSource source) noexcept {
  if (source == HexSource::kFrameStream) {
    return index_for_source(kStreamSource);
  }
  return index_for_source(source);
}

}  // namespace pnga::ui::qt
