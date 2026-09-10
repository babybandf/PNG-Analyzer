#include "frame_inspection_session.h"

#include <pnga/analysis-engine/stage_analysis.h>
#include <pnga/png-format/animation_index.h>
#include <pnga/png-reconstruction/rgba_delivery.h>

#include <QThread>

#include <algorithm>
#include <limits>
#include <utility>

namespace pnga::gui {

namespace {

// One-shot worker thread for a single queued completion. The session never
// waits on the UI thread: the thread finishes in the background and destroys
// itself through deleteLater.
class FrameInspectionWorker final : public QThread {
 public:
  FrameInspectionWorker(std::function<void()> job, QObject* parent)
      : QThread(parent), job_(std::move(job)) {}
  void run() override {
    if (job_) {
      job_();
    }
  }

 private:
  std::function<void()> job_;
};

}  // namespace

FrameInspectionSession::FrameInspectionSession(QObject* parent)
    : QObject(parent) {}

FrameInspectionSession::~FrameInspectionSession() {
  // Join in-flight one-shot worker threads before the members die (the
  // DocumentSession precedent; runtime switches never join on the UI
  // thread, only final destruction does). Completions are
  // generation-gated, so their publications are dropped, not delivered.
  const auto workers = findChildren<QThread*>();
  for (QThread* worker : workers) {
    if (worker->isRunning()) {
      worker->wait();
    }
  }
}

void FrameInspectionSession::selectTarget(
    std::shared_ptr<const pnga::analysis_engine::AnalysisTarget> target,
    pnga::trace_model::InspectionTicket ticket) {
  std::vector<pnga::trace_model::InspectionTicket> dropped;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // A new target supersedes every queued and in-flight request of the
    // previous context; the old artifacts are released here (shared_ptr
    // drops) and any still-running worker finishes in the background.
    for (const auto& job : queue_) {
      dropped.push_back(job.ticket);
    }
    for (const auto& job : in_flight_) {
      dropped.push_back(job.ticket);
    }
    queue_.clear();
    in_flight_.clear();
    retained_bytes_ = 0;  // the previous context's retained artifacts drop
    evidence_active_ = false;
    evidence_selection_ = pnga::trace_model::Selection{};
    evidence_key_ = pnga::trace_model::AnalysisKey{};
    current_target_ = std::move(target);
    current_ticket_ = ticket;
  }
  for (const auto& ticket_to_drop : dropped) {
    emit requestCancelled(ticket_to_drop);
  }
  emit targetSelected(target, ticket);
}

void FrameInspectionSession::clear(std::uint64_t next_generation) {
  std::vector<pnga::trace_model::InspectionTicket> dropped;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& job : queue_) {
      dropped.push_back(job.ticket);
    }
    for (const auto& job : in_flight_) {
      dropped.push_back(job.ticket);
    }
    queue_.clear();
    in_flight_.clear();
    evidence_active_ = false;
    evidence_selection_ = pnga::trace_model::Selection{};
    evidence_key_ = pnga::trace_model::AnalysisKey{};
    current_target_.reset();
    current_ticket_ = pnga::trace_model::InspectionTicket{};
    retained_bytes_ = 0;
    generation_ = next_generation;
  }
  for (const auto& ticket_to_drop : dropped) {
    emit requestCancelled(ticket_to_drop);
  }
}

bool FrameInspectionSession::accepts(
    const pnga::trace_model::InspectionTicket& ticket,
    pnga::trace_model::PublicationScope scope) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (current_target_ == nullptr) {
    return false;
  }
  return pnga::trace_model::accepts_publication(current_ticket_, ticket,
                                                scope);
}

std::shared_ptr<const pnga::analysis_engine::AnalysisTarget>
FrameInspectionSession::currentTarget() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_target_;
}

std::uint64_t FrameInspectionSession::retainedBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return retained_bytes_;
}

std::uint64_t FrameInspectionSession::reservedBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::uint64_t total = 0;
  for (const auto& job : queue_) {
    total += job.reservation;
  }
  for (const auto& job : in_flight_) {
    total += job.reservation;
  }
  return total;
}

void FrameInspectionSession::openFrame(
    const pnga::analysis_engine::FrameRequest& request,
    pnga::trace_model::InspectionTicket ticket,
    pnga::analysis_engine::JobPriority priority) {
  QueuedJob job;
  job.kind = JobKind::kFrameOpenAnalysis;
  job.ticket = ticket;
  job.open_request = request;
  job.priority = priority;
  job.reservation = kFrameAnalysisReservation;
  enqueue(std::move(job));
}

void FrameInspectionSession::requestFrameAnalysis(
    pnga::trace_model::InspectionTicket ticket,
    pnga::analysis_engine::JobPriority priority, std::uint64_t reservation) {
  QueuedJob job;
  job.kind = JobKind::kFrameAnalysis;
  job.ticket = ticket;
  job.priority = priority;
  job.reservation = reservation;
  enqueue(std::move(job));
}

void FrameInspectionSession::requestFrameStatistics(
    pnga::trace_model::InspectionTicket ticket,
    pnga::statistics::DocumentIdentity document,
    pnga::analysis_engine::JobPriority priority, std::uint64_t reservation) {
  QueuedJob job;
  job.kind = JobKind::kFrameStatistics;
  job.ticket = ticket;
  job.document = std::move(document);
  job.priority = priority;
  job.reservation = reservation;
  enqueue(std::move(job));
}

void FrameInspectionSession::setEvidenceFocus(
    pnga::trace_model::AnalysisKey source_key,
    pnga::trace_model::Selection source_selection,
    pnga::trace_model::InspectionTicket parent_ticket) {
  pnga::trace_model::Selection selection_copy = source_selection;
  std::uint64_t serial = 0;
  bool accepted = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // accepts() re-locks; inline the C1 gate while already holding the
    // session mutex.
    if (current_target_ != nullptr &&
        pnga::trace_model::accepts_publication(
            current_ticket_, parent_ticket,
            pnga::trace_model::PublicationScope::kTarget) &&
        evidence_serial_ != std::numeric_limits<std::uint64_t>::max()) {
      evidence_key_ = source_key;
      evidence_selection_ = std::move(source_selection);
      evidence_parent_ = parent_ticket;
      ++evidence_serial_;  // independent monotonic focus serial (checked)
      evidence_active_ = true;
      serial = evidence_serial_;
      accepted = true;
    }
  }
  if (accepted) {
    emit evidenceFocusChanged(source_key, selection_copy, parent_ticket,
                              serial);
  }
}

void FrameInspectionSession::clearEvidenceFocus() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!evidence_active_) {
      return;
    }
    evidence_active_ = false;
    evidence_selection_ = pnga::trace_model::Selection{};
    evidence_key_ = pnga::trace_model::AnalysisKey{};
    evidence_parent_ = pnga::trace_model::InspectionTicket{};
  }
  emit evidenceFocusCleared();
}

bool FrameInspectionSession::evidenceFocusActive() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return evidence_active_;
}

bool FrameInspectionSession::evidenceAccepts(std::uint64_t focus_serial) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return evidence_active_ && focus_serial == evidence_serial_;
}

std::uint64_t FrameInspectionSession::evidenceFocusSerial() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return evidence_serial_;
}

const pnga::trace_model::AnalysisKey*
FrameInspectionSession::evidenceSourceKey() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return evidence_active_ ? &evidence_key_ : nullptr;
}

void FrameInspectionSession::setExecutorForTesting(
    std::function<void(std::function<void()>)> executor) {
  std::lock_guard<std::mutex> lock(mutex_);
  executor_ = std::move(executor);
}

void FrameInspectionSession::enqueue(QueuedJob job) {
  std::vector<pnga::trace_model::InspectionTicket> dropped;
  bool pending_pump = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // The kFrameOpenAnalysis job exists to build and adopt the FIRST
    // target, so it is enqueued even before any target exists.
    if (current_target_ == nullptr &&
        job.kind != JobKind::kFrameOpenAnalysis) {
      dropped.push_back(job.ticket);
    } else if (job.reservation > kReservationBudget) {
      // A single job larger than the whole in-flight budget is rejected
      // up front; nothing else is disturbed.
      dropped.push_back(job.ticket);
    } else {
      // Duplicate requests for the same frame merge into the newest one.
      const pnga::trace_model::AnimationFrame* frame =
          std::get_if<pnga::trace_model::AnimationFrame>(
              &job.ticket.key.identity);
      const auto same_key = [&](const QueuedJob& queued) {
        if (queued.kind != job.kind) {
          return false;
        }
        const auto* queued_frame =
            std::get_if<pnga::trace_model::AnimationFrame>(
                &queued.ticket.key.identity);
        return frame != nullptr && queued_frame != nullptr &&
               queued_frame->index == frame->index;
      };
      const auto merge = std::find_if(queue_.begin(), queue_.end(), same_key);
      if (merge != queue_.end()) {
        dropped.push_back(merge->ticket);
        queue_.erase(merge);
      }
      if (queue_.size() >= kQueueCap) {
        // Full queue: drop the lowest-priority oldest queued request and
        // report its cancellation; the incoming user selection itself is
        // only dropped when the queue holds nothing lower-priority and the
        // incoming job is background work.
        const auto lower = [&](const QueuedJob& queued) {
          return static_cast<int>(queued.priority) <
                 static_cast<int>(job.priority);
        };
        auto victim = std::find_if(queue_.begin(), queue_.end(), lower);
        if (victim == queue_.end() &&
            job.priority != pnga::analysis_engine::JobPriority::kSelection) {
          // Background work never displaces user selections.
          dropped.push_back(job.ticket);
          pending_pump = false;
        } else {
          if (victim == queue_.end()) {
            // All queued jobs are selection priority: the oldest selection
            // is superseded by the newest user choice and never blocks it.
            victim = queue_.begin();
          }
          dropped.push_back(victim->ticket);
          queue_.erase(victim);
          job.arrival = ++arrival_counter_;
          queue_.push_back(std::move(job));
          pending_pump = true;
        }
      } else {
        job.arrival = ++arrival_counter_;
        queue_.push_back(std::move(job));
        pending_pump = true;
      }
      if (pending_pump) {
        pumpLocked();
      }
    }
  }
  for (const auto& entry : dropped) {
    emit requestCancelled(entry);
  }
  dispatchPumped();
}

// Priority order: current pixel (kSelection) first, then the visible panel
// (kViewport), then statistics (kBackground); FIFO within a priority.
void FrameInspectionSession::pumpLocked() {
  if (in_flight_.size() >= 1 || queue_.empty()) {
    return;
  }
  const auto order = [](const QueuedJob& left, const QueuedJob& right) {
    if (left.priority != right.priority) {
      return static_cast<int>(left.priority) >
             static_cast<int>(right.priority);
    }
    return left.arrival < right.arrival;
  };
  auto next = std::min_element(queue_.begin(), queue_.end(), order);
  QueuedJob job = *next;
  queue_.erase(next);
  in_flight_.push_back(
      InFlight{job.ticket, job.kind, job.reservation});
  const pnga::trace_model::InspectionTicket ticket = job.ticket;
  const JobKind kind = job.kind;
  const pnga::analysis_engine::FrameRequest open_request = job.open_request;
  const pnga::statistics::DocumentIdentity document = job.document;
  const pnga::analysis_engine::AnalysisTarget* target = current_target_.get();
  const std::uint64_t generation = generation_;
  const std::uint64_t reservation = job.reservation;
  std::shared_ptr<const pnga::analysis_engine::AnalysisTarget> kept_alive =
      current_target_;
  auto completion = [this, ticket, kind, open_request, document, target,
                     generation, reservation, kept_alive]() {
    // Every exit path releases the in-flight reservation exactly once.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto& active = in_flight_;
      const auto entry = std::find_if(
          active.begin(), active.end(),
          [&](const InFlight& job_in_flight) {
            return job_in_flight.ticket == ticket;
          });
      if (entry != active.end()) {
        active.erase(entry);
      }
    }
    if (generation != generation_) {
      return;  // closed or replaced: never touch the current context
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pumpLocked();  // keep the single execution worker fed
    }
    dispatchPumped();
    if (kind == JobKind::kFrameOpenAnalysis) {
      // Build the target off the UI thread (contract C1), adopt it under
      // the monotonic selection-serial guard and analyze in this job. The
      // adoption makes the ticket current, so the C1 gate below accepts it.
      auto built = pnga::analysis_engine::make_frame_target(open_request);
      if (!built.target) {
        emit frameAnalysisFailed(
            ticket,
            QString::fromStdString(
                built.error.empty() ? "frame target unavailable"
                                    : built.error));
        return;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto* incoming = std::get_if<pnga::trace_model::AnimationFrame>(
            &ticket.key.identity);
        const bool newer = current_ticket_.key.identity !=
                               pnga::trace_model::ImageIdentity{} &&
                           incoming == nullptr;
        (void)newer;
        const auto* current =
            std::get_if<pnga::trace_model::AnimationFrame>(
                &current_ticket_.key.identity);
        const bool adopt = current == nullptr ||
                           ticket.selection_serial >=
                               current_ticket_.selection_serial;
        if (adopt) {
          current_target_ = built.target;
          current_ticket_ = ticket;
        }
      }
      analyze_target_frame(ticket, built.target);
      return;
    }
    // C1 publication gate: stale results of superseded targets are dropped
    // after their reservation was already released.
    if (!accepts(ticket, pnga::trace_model::PublicationScope::kTarget)) {
      return;
    }
    if (kind == JobKind::kFrameAnalysis) {
      const auto* frame =
          std::get_if<pnga::trace_model::AnimationFrame>(
              &ticket.key.identity);
      if (target == nullptr || frame == nullptr) {
        emit frameAnalysisFailed(ticket,
                                 QStringLiteral("frame analysis has no target"));
        return;
      }
      analyze_target_frame(ticket, kept_alive);
      return;
    }
    pnga::analysis_engine::FrameStatisticsRequest stats_request;
    stats_request.target = kept_alive;
    const auto* frame =
        std::get_if<pnga::trace_model::AnimationFrame>(
            &ticket.key.identity);
    if (target == nullptr || frame == nullptr) {
      emit frameStatisticsFailed(ticket,
                                 QStringLiteral("frame statistics has no target"));
      return;
    }
    stats_request.document = document;
    const auto result =
        pnga::analysis_engine::collect_frame_statistics(stats_request, nullptr);
    auto shared = std::make_shared<const pnga::analysis_engine::FrameStatisticsResult>(
        std::move(result));
    if (!shared->error.empty()) {
      emit frameStatisticsFailed(ticket,
                                 QString::fromStdString(shared->error));
      return;
    }
    publishStatistics(std::move(shared), ticket);
  };
  if (executor_) {
    // The test executor may run the completion synchronously on the calling
    // thread; never invoke it while holding the session mutex.
    deferred_completion_ = std::move(completion);
    return;
  }
  auto* worker = new FrameInspectionWorker(std::move(completion), this);
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void FrameInspectionSession::dispatchPumped() {
  std::function<void()> to_run;
  std::function<void(std::function<void()>)> executor;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    to_run = std::move(deferred_completion_);
    deferred_completion_ = nullptr;
    executor = executor_;
  }
  if (to_run && executor) {
    executor(std::move(to_run));
  }
}

void FrameInspectionSession::analyze_target_frame(
    const pnga::trace_model::InspectionTicket& ticket,
    const std::shared_ptr<const pnga::analysis_engine::AnalysisTarget>&
        target) {
  if (target == nullptr) {
    emit frameAnalysisFailed(ticket,
                             QStringLiteral("frame analysis has no target"));
    return;
  }
  auto stages = pnga::analysis_engine::analyze_stages(
      *target->stream, target->header,
      pnga::analysis_engine::DecodeLimits{}, nullptr);
  if (!stages.success) {
    emit frameAnalysisFailed(
        ticket,
        QString::fromStdString(
            stages.error.empty() ? "frame analysis failed" : stages.error));
    return;
  }
  auto delivered = pnga::png_reconstruction::deliver_rgba8(
      stages.native, target->delivery, 64ull << 20, [] { return false; });
  if (!delivered.success) {
    emit frameAnalysisFailed(
        ticket,
        QString::fromStdString(
            delivered.error.empty() ? "frame delivery failed"
                                    : delivered.error));
    return;
  }
  auto frame_set = std::make_shared<pnga::analysis_engine::FrameStageSet>();
  frame_set->identity = ticket.key.identity;
  frame_set->control = target->control.value_or(
      pnga::png_format::FrameControl{});
  frame_set->stages = std::move(stages);
  frame_set->delivered = std::move(delivered.image);
  publishAnalysis(*frame_set, ticket);
}

void FrameInspectionSession::publishAnalysis(
    const pnga::analysis_engine::FrameStageSet& frame,
    const pnga::trace_model::InspectionTicket& ticket) {
  if (!accepts(ticket, pnga::trace_model::PublicationScope::kTarget)) {
    return;
  }
  std::uint64_t bytes = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bytes = frame_stage_set_bytes(frame);
    // Retained budget: keep the newest artifacts; a frame that would exceed
    // the frozen budget replaces the retained context entirely rather than
    // growing without bound.
    if (bytes > kRetainedBudget) {
      retained_bytes_ = 0;
    } else {
      retained_bytes_ = bytes;
    }
  }
  emit frameAnalysisReady(
      std::make_shared<const pnga::analysis_engine::FrameStageSet>(frame),
      ticket);
}

void FrameInspectionSession::publishStatistics(
    std::shared_ptr<const pnga::analysis_engine::FrameStatisticsResult> result,
    const pnga::trace_model::InspectionTicket& ticket) {
  if (!accepts(ticket, pnga::trace_model::PublicationScope::kTarget)) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  retained_bytes_ = std::min<std::uint64_t>(
      kRetainedBudget,
      retained_bytes_ + result->value.snapshot.chunks.data.data_bytes);
  emit frameStatisticsReady(std::move(result), ticket);
}

std::uint64_t FrameInspectionSession::frame_stage_set_bytes(
    const pnga::analysis_engine::FrameStageSet& frame) {
  return frame.stages.filtered.size() + frame.stages.unfiltered.size() +
         frame.delivered.pixels.size();
}

}  // namespace pnga::gui
