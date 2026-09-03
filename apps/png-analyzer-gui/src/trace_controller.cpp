// WP-5U15: bounded trace pipeline moved verbatim from the facade, including
// its budgets, checked arithmetic, deduplication and cancellation policy.
// WP-5U12E: the Decode Trace page navigates through the typed
// CompressionSelectionStore targets only (Show in Hex carries the compressed
// DeflateBitRange with every physical file span, Show inflated output carries
// the InflatedByteRange); no untyped integer navigation signal exists and no
// path here submits a replay except the explicit Open Decode Trace action.

#include "trace_controller.h"

#include "selection_navigation_controller.h"

#include <pnga/analysis-engine/trace_query.h>

#include <pnga/ui/qt/block_inspector.h>
#include <pnga/ui/qt/decode_trace_inspector.h>
#include <pnga/ui/qt/hex_view.h>
#include <pnga/ui/qt/trace_inspector_binding.h>

#include <QMetaObject>

#include <limits>

namespace {

// Bounded Deep Trace budgets (WP-5U13). max_tokens is the primary row bound;
// the output budget caps how much of the deflate stream a replay decodes.
constexpr std::uint64_t kMaxTraceTokens = 4096;
// Keep enough bounded replay budget for wide RGB rows near the end of a
// moderately large image while retaining a hard upper bound.
constexpr std::uint64_t kTraceOutputBudgetBytes = 1ull << 23;   // 8 MiB
constexpr std::uint64_t kTraceIndexOutputBytes = 1ull << 26;    // 64 MiB

}  // namespace

TraceController::TraceController(MainWindowWidgets widgets, QObject* parent)
    : QObject(parent), w_(widgets) {
  trace_state_ =
      std::make_unique<pnga::analysis_engine::TraceInspectorStateMachine>();
  // WP-5U12C/E: the Compression pages navigate through the shared
  // CompressionSelectionStore and the selection controller; only the explicit
  // Open Decode Trace action reaches this controller, and it reuses the
  // existing bounded request path once per interval.
  connect(w_.block_inspector,
          &pnga::ui::qt::BlockInspector::decodeTraceRequested, this,
          [this](std::uint64_t generation, std::uint64_t /*block_index*/,
                 pnga::trace_model::InflatedByteRange output_range) {
            if (w_.trace_binding == nullptr || trace_state_ == nullptr ||
                trace_ == nullptr || !trace_->has_index() ||
                generation != generation_ || !output_range.valid() ||
                output_range.empty()) {
              return;
            }
            // The explicit action goes through the same bounded request path
            // as a committed pixel: it re-arms the trace status display.
            w_.trace_binding->setNotIndexed(false);
            // WP-5U12 audit 7.1: the explicit drill-down reveals the Decode
            // Trace page (Blocks=0, Huffman=1, Decode Trace=2). Switching the
            // page submits no trace work of its own.
            if (w_.compression_inspector_tabs != nullptr) {
              w_.compression_inspector_tabs->setCurrentIndex(2);
            }
            const std::uint64_t begin = output_range.begin.value;
            const std::uint64_t end = output_range.end.value;
            if (trace_interval_.has_value() &&
                trace_request_generation_ == generation_ &&
                trace_interval_->first == begin &&
                trace_interval_->second == end) {
              if (trace_result_ != nullptr) {
                const bool accepted = trace_state_->publish(
                    *trace_result_, std::nullopt,
                    trace_selected_output_offset_.value_or(
                        trace_result_->inflated_begin),
                    trace_scanline_);
                if (accepted) {
                  w_.trace_binding->publishState(trace_state_->state());
                }
              }
              return;  // identical committed interval already submitted
            }
            pending_trace_coordinate_.reset();
            trace_interval_ = std::make_pair(begin, end);
            trace_request_generation_ = generation_;
            if (trace_handle_ != nullptr && trace_handle_->accepted()) {
              trace_->cancel(*trace_handle_);
#ifdef PNGA_TRACE_CONTROLLER_TESTING
              ++cancelled_requests_;
#endif
              trace_handle_.reset();
            }
            pnga::analysis_engine::TraceOrchestrationRequest request;
            request.generation = generation_;
            pnga::trace_model::Selection selection;
            selection.stage = pnga::trace_model::Stage::kDelivered;
            request.selection = selection;
            request.inflated_begin = begin;
            request.inflated_end = end;
            request.max_tokens = kMaxTraceTokens;
            request.trace_output_budget_bytes = kTraceOutputBudgetBytes;
            request.priority =
                pnga::analysis_engine::JobPriority::kSelection;
            trace_state_->markReplaying(generation_);
            w_.trace_binding->publishState(trace_state_->state());
            const auto handle = trace_->submit(request);
#ifdef PNGA_TRACE_CONTROLLER_TESTING
            if (handle.accepted()) {
              ++accepted_requests_;
            }
#endif
            trace_handle_ =
                handle.accepted()
                    ? std::make_unique<
                          pnga::analysis_engine::TraceTaskHandle>(handle)
                    : nullptr;
          });
  // WP-5U12 audit 7.1: a Huffman occurrence drill-down navigates through the
  // shared CompressionSelectionStore (applyNavigation); a Huffman row
  // selection uses setManual and emits no navigationRequested, so only the
  // explicit occurrence action reveals the Decode Trace page. No trace work
  // is submitted here. In standalone controller tests no store exists and
  // the connection is skipped.
  auto* compression_store =
      w_.block_inspector != nullptr
          ? w_.block_inspector->window()->findChild<
                pnga::ui::qt::CompressionSelectionStore*>()
          : nullptr;
  if (compression_store != nullptr) {
    connect(compression_store,
            &pnga::ui::qt::CompressionSelectionStore::navigationRequested,
            this,
            [this](const pnga::trace_model::CompressionNavigationTarget&
                       target) {
              if (target.origin == pnga::trace_model::
                                               CompressionNavigationOrigin::
                                               kHuffman &&
                  w_.compression_inspector_tabs != nullptr) {
                w_.compression_inspector_tabs->setCurrentIndex(2);
              }
            });
  }
}

void TraceController::replaceDocument(
    std::uint64_t generation,
    std::shared_ptr<const pnga::io::IByteSource> source) {
  trace_.reset();
  trace_handle_.reset();
  trace_result_.reset();
  pending_trace_coordinate_.reset();
  trace_scanline_.reset();
  trace_selected_output_offset_.reset();
  trace_interval_.reset();
  trace_request_generation_ = 0;
  trace_deflate_data_begin_ = 0;
  generation_ = generation;
  if (trace_state_ != nullptr) {
    trace_state_->replaceDocument(generation);
  }
  if (w_.trace_binding != nullptr) {
    w_.trace_binding->clear();
    w_.trace_binding->setHasDocument(true);
  }
  if (!source) {
    return;
  }
  auto trace = std::make_unique<pnga::analysis_engine::TraceOrchestrator>(
      /*worker_count=*/1,
      /*max_reserved_bytes=*/kTraceOutputBudgetBytes * 2);
  if (!trace->open(source, kTraceIndexOutputBytes)) {
    return;
  }
  trace->setDocumentGeneration(generation);
  if (w_.trace_binding != nullptr) {
    w_.trace_binding->publishFastIndex(trace->fast_index());
  }
  // Bridge the worker-thread result callback onto the GUI thread. The queued
  // invoke is dropped automatically if this controller is destroyed.
  trace->setResultCallback(
      [this](const pnga::analysis_engine::TraceQueryResult& result) {
        QMetaObject::invokeMethod(
            this, [this, result] { onTraceResult(result); },
            Qt::QueuedConnection);
      });
  trace_ = std::move(trace);
  if (pending_trace_coordinate_.has_value()) {
    const pnga::trace_model::ImageCoordinate pending =
        *pending_trace_coordinate_;
    pending_trace_coordinate_.reset();
    requestFor(pending);
  }
}

void TraceController::clearDocument(std::uint64_t generation) {
  generation_ = generation;
  trace_.reset();
  trace_handle_.reset();
  trace_result_.reset();
  pending_trace_coordinate_.reset();
  trace_scanline_.reset();
  trace_selected_output_offset_.reset();
  trace_interval_.reset();
  trace_request_generation_ = 0;
  trace_deflate_data_begin_ = 0;
  if (trace_state_ != nullptr) {
    trace_state_->replaceDocument(generation);
  }
  if (w_.trace_binding != nullptr) {
    w_.trace_binding->clear();
    w_.trace_binding->setHasDocument(false);
  }
}

void TraceController::setQueryCoordinator(
    pnga::analysis_engine::QueryCoordinator* query) {
  query_ = query;
  if (pending_trace_coordinate_.has_value()) {
    const pnga::trace_model::ImageCoordinate pending =
        *pending_trace_coordinate_;
    pending_trace_coordinate_.reset();
    requestFor(pending);
  }
}

void TraceController::setSelectedOutputOffset(
    std::optional<std::uint64_t> output_offset) {
  trace_selected_output_offset_ = std::move(output_offset);
}

void TraceController::setSelectedScanline(std::optional<std::uint64_t> scanline) {
  trace_scanline_ = std::move(scanline);
}

std::uint64_t TraceController::generation() const noexcept {
  return generation_;
}

#ifdef PNGA_TRACE_CONTROLLER_TESTING
std::size_t TraceController::acceptedRequestCountForTest() const noexcept {
  return accepted_requests_;
}

std::size_t TraceController::cancelledRequestCountForTest() const noexcept {
  return cancelled_requests_;
}
#endif

void TraceController::onTraceResult(
    const pnga::analysis_engine::TraceQueryResult& result) {
  if (result.generation != generation_ || w_.trace_binding == nullptr ||
      trace_state_ == nullptr) {
    return;  // stale result; never publish for an older document
  }
  trace_result_ =
      std::make_shared<const pnga::analysis_engine::TraceQueryResult>(result);
  const std::uint64_t selected_output_offset =
      trace_selected_output_offset_.value_or(result.inflated_begin);
  const bool accepted = trace_state_->publish(
      result, std::nullopt, selected_output_offset, trace_scanline_);
  if (accepted) {
    w_.trace_binding->publishState(trace_state_->state());
  }
  trace_handle_.reset();
}

void TraceController::requestFor(
    const pnga::trace_model::ImageCoordinate& coordinate) {
  if (w_.trace_binding == nullptr || trace_state_ == nullptr) {
    return;
  }
  if (trace_ == nullptr || !trace_->has_index() || query_ == nullptr ||
      !query_->has_index()) {
    // The trace pipeline is not ready yet; remember the committed coordinate
    // and publish a not-indexed state instead of guessing an interval.
    pending_trace_coordinate_ = coordinate;
    w_.trace_binding->setNotIndexed(true);
    return;
  }
  w_.trace_binding->setNotIndexed(false);
  const auto row = pnga::analysis_engine::stream_row_for_pixel(
      query_->anchors().layout, coordinate.x, coordinate.y);
  if (!row.has_value()) {
    return;
  }
  const auto& scanlines = query_->anchors().scanlines;
  if (*row >= scanlines.size()) {
    return;
  }
  const std::uint64_t begin = scanlines[*row].offset;
  if (scanlines[*row].length >
      std::numeric_limits<std::uint64_t>::max() - begin) {
    trace_selected_output_offset_.reset();
    return;
  }
  const std::uint64_t end = begin + scanlines[*row].length;
  if (end <= begin) {
    trace_selected_output_offset_.reset();
    return;
  }
  trace_selected_output_offset_ = filtered_output_offset_for_pixel(
      query_->anchors(), coordinate, *row);
  if (trace_interval_.has_value() && trace_request_generation_ == generation_ &&
      trace_interval_->first == begin && trace_interval_->second == end) {
    if (trace_result_ != nullptr) {
      const bool accepted = trace_state_->publish(
          *trace_result_, std::nullopt,
          trace_selected_output_offset_.value_or(trace_result_->inflated_begin),
          trace_scanline_);
      if (accepted) {
        w_.trace_binding->publishState(trace_state_->state());
      }
    }
    return;  // identical committed interval already requested (dedup)
  }
  pending_trace_coordinate_.reset();
  trace_interval_ = std::make_pair(begin, end);
  trace_scanline_ = *row;
  trace_request_generation_ = generation_;
  if (trace_handle_ != nullptr && trace_handle_->accepted()) {
    trace_->cancel(*trace_handle_);
#ifdef PNGA_TRACE_CONTROLLER_TESTING
    ++cancelled_requests_;
#endif
    trace_handle_.reset();
  }
  pnga::analysis_engine::TraceOrchestrationRequest request;
  request.generation = generation_;
  pnga::trace_model::Selection selection;
  selection.image = coordinate;
  selection.stage = pnga::trace_model::Stage::kDelivered;
  request.selection = selection;
  request.inflated_begin = begin;
  request.inflated_end = end;
  request.max_tokens = kMaxTraceTokens;
  request.trace_output_budget_bytes = kTraceOutputBudgetBytes;
  request.priority = pnga::analysis_engine::JobPriority::kSelection;
  trace_state_->markReplaying(generation_);
  w_.trace_binding->publishState(trace_state_->state());
  const auto handle = trace_->submit(request);
#ifdef PNGA_TRACE_CONTROLLER_TESTING
  if (handle.accepted()) {
    ++accepted_requests_;
  }
#endif
  trace_handle_ = handle.accepted()
                      ? std::make_unique<pnga::analysis_engine::TraceTaskHandle>(
                            handle)
                      : nullptr;
}
