// FrameInspectionSession (WP-APNG-INSPECT contract C5, plan T07): the GUI
// owner of one APNG document's per-frame inspection context. It adopts the
// immutable AnalysisTarget built off the UI thread, schedules bounded
// background analysis with the frozen C5 budgets (64 MiB retained, 64 MiB
// in-flight reservation, one execution worker, queue cap 8, priority pixel
// -> visible panel -> statistics) and gates every publication through the
// C1 accepts_publication rule. Static documents never create this session.

#ifndef PNGA_GUI_FRAME_INSPECTION_SESSION_H
#define PNGA_GUI_FRAME_INSPECTION_SESSION_H

#include <pnga/analysis-engine/analysis_target.h>
#include <pnga/analysis-engine/frame_analysis.h>
#include <pnga/analysis-engine/frame_statistics.h>
#include <pnga/analysis-engine/job_scheduler.h>
#include <pnga/trace-model/inspection_context.h>

#include <QObject>
#include <QString>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace pnga::gui {

class FrameInspectionSession final : public QObject {
  Q_OBJECT

 public:
  explicit FrameInspectionSession(QObject* parent = nullptr);
  ~FrameInspectionSession() override;

  FrameInspectionSession(const FrameInspectionSession&) = delete;
  FrameInspectionSession& operator=(const FrameInspectionSession&) = delete;

  // Adopts an already-built immutable target as the current inspection
  // context. The publication gate resets to `ticket`; stale results from
  // earlier contexts are discarded through accepts().
  void selectTarget(
      std::shared_ptr<const pnga::analysis_engine::AnalysisTarget> target,
      pnga::trace_model::InspectionTicket ticket);

  // Clears the inspection context for a document switch or close. In-flight
  // jobs are cancelled; their completions never touch this session again.
  void clear(std::uint64_t next_generation);

  // C1 publication gate for results arriving from any thread.
  bool accepts(const pnga::trace_model::InspectionTicket& ticket,
               pnga::trace_model::PublicationScope scope) const;

  std::uint64_t retainedBytes() const;
  std::uint64_t reservedBytes() const;

  // Analysis requests. Priority orders the queue (kSelection = current
  // pixel, kViewport = visible panel, kBackground = statistics); duplicate
  // requests for the same frame merge into the newest one. `reservation`
  // defaults to the kind's frozen budget share and is rejectable (a job
  // whose reservation exceeds the in-flight budget is cancelled, never run).
  void requestFrameAnalysis(
      pnga::trace_model::InspectionTicket ticket,
      pnga::analysis_engine::JobPriority priority =
          pnga::analysis_engine::JobPriority::kSelection,
      std::uint64_t reservation = kFrameAnalysisReservation);
  void requestFrameStatistics(
      pnga::trace_model::InspectionTicket ticket,
      pnga::statistics::DocumentIdentity document,
      pnga::analysis_engine::JobPriority priority =
          pnga::analysis_engine::JobPriority::kBackground,
      std::uint64_t reservation = kFrameStatisticsReservation);

  // Test seam: injects a deterministic executor. When set, jobs run through
  // it (typically synchronously, or stored and run at a chosen point)
  // instead of the background worker thread, so tests control completion
  // order without sleeping.
  void setExecutorForTesting(
      std::function<void(std::function<void()>)> executor);

  // Frozen C5 budget shares (frame analysis/index/trace/statistics retained
  // combined; in-flight reservation combined).
  static constexpr std::uint64_t kRetainedBudget = 64ull << 20;
  static constexpr std::uint64_t kReservationBudget = 64ull << 20;
  static constexpr std::size_t kQueueCap = 8;
  static constexpr std::uint64_t kFrameAnalysisReservation = 8ull << 20;
  static constexpr std::uint64_t kFrameStatisticsReservation = 16ull << 20;

 signals:
  void targetSelected(
      std::shared_ptr<const pnga::analysis_engine::AnalysisTarget> target,
      pnga::trace_model::InspectionTicket ticket);
  void frameAnalysisReady(
      std::shared_ptr<const pnga::analysis_engine::FrameStageSet> frame,
      pnga::trace_model::InspectionTicket ticket);
  void frameAnalysisFailed(pnga::trace_model::InspectionTicket ticket,
                           const QString& error);
  void frameStatisticsReady(
      std::shared_ptr<const pnga::analysis_engine::FrameStatisticsResult>
          result,
      pnga::trace_model::InspectionTicket ticket);
  void frameStatisticsFailed(pnga::trace_model::InspectionTicket ticket,
                             const QString& error);
  void requestCancelled(pnga::trace_model::InspectionTicket ticket);

 private:
  enum class JobKind { kFrameAnalysis, kFrameStatistics };

  struct QueuedJob {
    JobKind kind = JobKind::kFrameAnalysis;
    pnga::trace_model::InspectionTicket ticket;
    pnga::statistics::DocumentIdentity document;
    pnga::analysis_engine::JobPriority priority =
        pnga::analysis_engine::JobPriority::kSelection;
    std::uint64_t arrival = 0;
    std::uint64_t reservation = 0;
  };

  struct InFlight {
    pnga::trace_model::InspectionTicket ticket;
    JobKind kind = JobKind::kFrameAnalysis;
    std::uint64_t reservation = 0;
  };

  void enqueue(QueuedJob job);
  void pumpLocked();
  void dispatchPumped();
  void publishAnalysis(const pnga::analysis_engine::FrameStageSet& frame,
                       const pnga::trace_model::InspectionTicket& ticket);
  void publishStatistics(
      std::shared_ptr<const pnga::analysis_engine::FrameStatisticsResult>
          result,
      const pnga::trace_model::InspectionTicket& ticket);
  static std::uint64_t frame_stage_set_bytes(
      const pnga::analysis_engine::FrameStageSet& frame);

  mutable std::mutex mutex_;
  std::shared_ptr<const pnga::analysis_engine::AnalysisTarget> current_target_;
  pnga::trace_model::InspectionTicket current_ticket_;
  std::uint64_t generation_ = 0;
  std::deque<QueuedJob> queue_;
  std::deque<InFlight> in_flight_;
  std::uint64_t retained_bytes_ = 0;
  std::uint64_t arrival_counter_ = 0;
  std::function<void(std::function<void()>)> executor_;
  std::function<void()> deferred_completion_;
};

}  // namespace pnga::gui

#endif  // PNGA_GUI_FRAME_INSPECTION_SESSION_H
