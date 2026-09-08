#ifndef PNG_ANALYZER_GUI_PNG_FILE_FILTER_H
#define PNG_ANALYZER_GUI_PNG_FILE_FILTER_H

#include <QFileInfo>
#include <QString>

// File-picker/drop filter only. File contents remain the authority for PNG or
// APNG capability after the path has passed this presentation predicate.
inline bool hasSupportedPngSuffix(const QString& path) {
  const QString suffix = QFileInfo(path).suffix();
  return suffix.compare(QStringLiteral("png"), Qt::CaseInsensitive) == 0 ||
         suffix.compare(QStringLiteral("apng"), Qt::CaseInsensitive) == 0;
}

#endif  // PNG_ANALYZER_GUI_PNG_FILE_FILTER_H
