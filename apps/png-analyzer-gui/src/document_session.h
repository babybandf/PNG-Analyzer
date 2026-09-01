#ifndef PNG_ANALYZER_GUI_DOCUMENT_SESSION_H
#define PNG_ANALYZER_GUI_DOCUMENT_SESSION_H

// WP-5U15: single owner of the current document: mapped source, chunk index,
// immutable stage set, validation report, decode/chunk-detail results, the
// QueryCoordinator and its status bridge, the four primary workers and the
// document generation. Replacement and close increment the generation before
// clearing state; a worker result whose generation differs from the current
// one is discarded before any controller or widget sees it.

#include "document_workers.h"

#include <pnga/analysis-engine/query_coordinator.h>
#include <pnga/analysis-engine/reference_decode.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/analysis-engine/validation.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_detail.h>
#include <pnga/png-format/chunk_index.h>

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

namespace pnga::png_reconstruction {
struct ImageHeader;
}

class DocumentSession final : public QObject {
  Q_OBJECT
 public:
  explicit DocumentSession(QObject* parent = nullptr);
  ~DocumentSession() override;

  // Maps `path`; returns false without changing any state when the file
  // cannot be read. On success takes shared ownership, increments the
  // generation, indexes the chunks, clears prior results/query/workers and
  // emits replaced(generation).
  bool replace(const QString& path);

  // Increments the generation first, clears all document state and emits
  // closed(generation).
  void close();

  // Starts Validation, Decode and Stage workers for the current document.
  void startPrimaryWorkers();

  // Creates the QueryCoordinator for the current source and stage header.
  void openQueryCoordinator(
      const pnga::png_reconstruction::ImageHeader& header);

  // Spawns the chunk detail worker with the same generation-and-selection
  // gate the facade used before extraction.
  void requestChunkDetail(const pnga::png_format::ChunkNode& node,
                          std::uint64_t selection_serial);

  std::uint64_t generation() const noexcept;
  bool hasDocument() const noexcept;
  const QString& currentFilePath() const noexcept;
  const pnga::png_format::ChunkIndex& index() const noexcept;
  std::shared_ptr<const pnga::io::IByteSource> source() const;
  const pnga::backend_libpng::ReferenceResult& decodeResult() const noexcept;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stageSet() const;
  const pnga::analysis_engine::DocumentValidationReport& validationReport()
      const noexcept;
  const pnga::png_format::ChunkDetail& chunkDetail() const noexcept;
  pnga::analysis_engine::QueryCoordinator* queryCoordinator() noexcept;

 signals:
  void replaced(std::uint64_t generation);
  void closed(std::uint64_t generation);
  void decodePublished(std::uint64_t generation);
  void stagesPublished(std::uint64_t generation);
  void validationPublished(std::uint64_t generation);
  void chunkDetailPublished(std::uint64_t generation,
                            std::uint64_t selection_serial);
  void rowQueryStatus(std::uint64_t row, int status);

 private slots:
  void onDecodeDone(std::uint64_t generation);
  void onStageDone(std::uint64_t generation);
  void onValidationDone(std::uint64_t generation);
  void onChunkDetailDone(std::uint64_t generation,
                         std::uint64_t selection_serial);

 private:
  void startDecode();
  void startStageAnalysis();
  void startValidation();

  std::shared_ptr<pnga::io::IByteSource> source_;
  pnga::png_format::ChunkIndex index_;
  std::shared_ptr<const pnga::analysis_engine::StageSet> stage_set_;
  pnga::analysis_engine::DocumentValidationReport validation_report_;
  pnga::backend_libpng::ReferenceResult decode_result_;
  pnga::png_format::ChunkDetail chunk_detail_;
  DecodeWorker* decode_worker_ = nullptr;
  StageWorker* stage_worker_ = nullptr;
  ValidationWorker* validation_worker_ = nullptr;
  ChunkDetailWorker* chunk_detail_worker_ = nullptr;
  std::unique_ptr<pnga::analysis_engine::QueryCoordinator> query_;
  QueryStatusBridge* query_bridge_ = nullptr;
  std::uint64_t generation_ = 0;
  std::uint64_t requested_detail_serial_ = 0;
  QString current_file_path_;
};

#endif  // PNG_ANALYZER_GUI_DOCUMENT_SESSION_H
