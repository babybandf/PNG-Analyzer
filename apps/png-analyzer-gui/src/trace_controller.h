#ifndef PNG_ANALYZER_GUI_TRACE_CONTROLLER_H
#define PNG_ANALYZER_GUI_TRACE_CONTROLLER_H

// WP-5U15: bounded Deep Trace orchestration moved verbatim from the facade.
// One TraceOrchestrator per document generation, the 4096-token and 8 MiB
// output budgets, interval deduplication and task cancellation. Worker
// callbacks reach the GUI thread through queued invocation; page switching,
// hover, resize and numeric-base changes never submit a replay.

#include "main_window_ui.h"

#include <pnga/analysis-engine/trace_inspector_state.h>
#include <pnga/analysis-engine/trace_orchestrator.h>
#include <pnga/trace-model/selection.h>
#include <pnga/ui/qt/selection_view_state.h>

#include <QObject>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace pnga::analysis_engine {
class QueryCoordinator;
}

class TraceController final : public QObject {
  Q_OBJECT
 public:
  explicit TraceController(MainWindowWidgets widgets,
                           QObject* parent = nullptr);

  // Resets every trace object, adopts the generation, clears the binding and
  // sets has-document true, then opens the orchestrator for `source` and
  // publishes its fast index. A committed-but-unreplayed coordinate is kept.
  void replaceDocument(std::uint64_t generation,
                       std::shared_ptr<const pnga::io::IByteSource> source);

  // Cancels an accepted task, resets interval/result state, replaces the
  // state-machine generation, clears the binding and sets has-document false.
  void clearDocument(std::uint64_t generation);

  // Supplies the query coordinator once the stage worker published it; a
  // pending committed coordinate is replayed here (the original facade
  // replayed it from openTraceCoordinator()).
  void setQueryCoordinator(pnga::analysis_engine::QueryCoordinator* query);

  void requestFor(const pnga::trace_model::ImageCoordinate& coordinate);
  void setSelectedOutputOffset(std::optional<std::uint64_t> output_offset);
  void setSelectedScanline(std::optional<std::uint64_t> scanline);

  std::uint64_t generation() const noexcept;

 signals:
  void hexSourceRequested(pnga::ui::qt::HexSource source);
  void hexRangeRequested(std::uint64_t begin, std::uint64_t end);

#ifdef PNGA_TRACE_CONTROLLER_TESTING
 public:
  std::size_t acceptedRequestCountForTest() const noexcept;
  std::size_t cancelledRequestCountForTest() const noexcept;
#endif

 private:
  void onTraceResult(const pnga::analysis_engine::TraceQueryResult& result);

  MainWindowWidgets w_;
  std::unique_ptr<pnga::analysis_engine::TraceOrchestrator> trace_;
  std::unique_ptr<pnga::analysis_engine::TraceInspectorStateMachine> trace_state_;
  std::unique_ptr<pnga::analysis_engine::TraceTaskHandle> trace_handle_;
  std::shared_ptr<const pnga::analysis_engine::TraceQueryResult> trace_result_;
  std::optional<pnga::trace_model::ImageCoordinate> pending_trace_coordinate_;
  std::optional<std::uint64_t> trace_scanline_;
  // Absolute inflated byte corresponding to the selected pixel's sample
  // (the scanline filter byte is intentionally excluded).
  std::optional<std::uint64_t> trace_selected_output_offset_;
  std::optional<std::pair<std::uint64_t, std::uint64_t>> trace_interval_;
  std::uint64_t trace_request_generation_ = 0;
  std::uint64_t trace_deflate_data_begin_ = 0;
  pnga::analysis_engine::QueryCoordinator* query_ = nullptr;
  std::uint64_t generation_ = 0;
#ifdef PNGA_TRACE_CONTROLLER_TESTING
  std::size_t accepted_requests_ = 0;
  std::size_t cancelled_requests_ = 0;
#endif
};

#endif  // PNG_ANALYZER_GUI_TRACE_CONTROLLER_H
