#ifndef PNG_ANALYZER_GUI_DOCUMENT_WORKERS_H
#define PNG_ANALYZER_GUI_DOCUMENT_WORKERS_H

// WP-5U15: QThread worker types and the QueryCoordinator status bridge,
// moved verbatim from the MainWindow facade. Ownership semantics are
// unchanged: workers keep their own source copy (Decode) or share it
// (Stage/Validation/ChunkDetail) so a newer document never invalidates an
// in-flight job.

#include <pnga/analysis-engine/reference_decode.h>
#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/analysis-engine/validation.h>
#include <pnga/io/byte_source.h>
#include <pnga/png-format/chunk_detail.h>
#include <pnga/png-format/chunk_index.h>

#include <QObject>
#include <QThread>

#include <cstdint>
#include <memory>

// Decodes a shared source on a worker thread. Owns its own source copy so a
// newly opened file cannot invalidate an in-flight decode.
class DecodeWorker final : public QThread {
  Q_OBJECT
 public:
  DecodeWorker(std::uint64_t generation,
               std::shared_ptr<pnga::io::IByteSource> source,
               QObject* parent = nullptr);

  std::uint64_t generation() const noexcept { return generation_; }
  pnga::backend_libpng::ReferenceResult result() const { return result_; }

 signals:
  void decodeDone(std::uint64_t generation);

 protected:
  void run() override;

 private:
  std::uint64_t generation_;
  std::shared_ptr<pnga::io::IByteSource> source_;
  pnga::backend_libpng::ReferenceResult result_;
};

// Materializes the Filtered/Unfiltered/Native stage set on a worker thread.
// Shares the source ownership so a newly opened file cannot invalidate it.
class StageWorker final : public QThread {
  Q_OBJECT
 public:
  StageWorker(std::uint64_t generation,
              std::shared_ptr<pnga::io::IByteSource> source,
              QObject* parent = nullptr);

  std::uint64_t generation() const noexcept { return generation_; }
  std::shared_ptr<const pnga::analysis_engine::StageSet> result() const {
    return result_;
  }

 signals:
  void stageDone(std::uint64_t generation);

 protected:
  void run() override;

 private:
  std::uint64_t generation_;
  std::shared_ptr<pnga::io::IByteSource> source_;
  std::shared_ptr<pnga::analysis_engine::StageSet> result_;
};

// Runs the complete Qt-free validation bundle off the GUI thread. The copied
// ChunkIndex preserves deterministic structure while shared source ownership
// keeps every borrowed range alive until publication.
class ValidationWorker final : public QThread {
  Q_OBJECT
 public:
  ValidationWorker(std::uint64_t generation,
                   std::shared_ptr<pnga::io::IByteSource> source,
                   pnga::png_format::ChunkIndex index,
                   QObject* parent = nullptr);

  std::uint64_t generation() const noexcept { return generation_; }
  pnga::analysis_engine::DocumentValidationReport result() const {
    return result_;
  }

 signals:
  void validationDone(std::uint64_t generation);

 protected:
  void run() override;

 private:
  std::uint64_t generation_;
  std::shared_ptr<pnga::io::IByteSource> source_;
  pnga::png_format::ChunkIndex index_;
  pnga::analysis_engine::DocumentValidationReport result_;
};

class ChunkDetailWorker final : public QThread {
  Q_OBJECT
 public:
  ChunkDetailWorker(std::uint64_t generation, std::uint64_t selection_serial,
                    std::shared_ptr<pnga::io::IByteSource> source,
                    pnga::png_format::ChunkNode node,
                    QObject* parent = nullptr);

  std::uint64_t generation() const noexcept { return generation_; }
  std::uint64_t selectionSerial() const noexcept { return selection_serial_; }
  const pnga::png_format::ChunkDetail& result() const noexcept { return result_; }

 signals:
  void detailDone(std::uint64_t generation, std::uint64_t selection_serial);

 protected:
  void run() override;

 private:
  std::uint64_t generation_;
  std::uint64_t selection_serial_;
  std::shared_ptr<pnga::io::IByteSource> source_;
  pnga::png_format::ChunkNode node_;
  pnga::png_format::ChunkDetail result_;
};

// Bridges the Qt-free QueryCoordinator's worker-thread status callback onto the
// GUI thread via a queued signal.
class QueryStatusBridge final : public QObject {
  Q_OBJECT
 public:
  explicit QueryStatusBridge(QObject* parent = nullptr) : QObject(parent) {}
 signals:
  void rowStatus(std::uint64_t row, int status);
};

#endif  // PNG_ANALYZER_GUI_DOCUMENT_WORKERS_H
