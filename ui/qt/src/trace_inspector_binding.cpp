// M5 Trace Gate Qt boundary binding.

#include "pnga/ui/qt/trace_inspector_binding.h"

#include "pnga/ui/qt/block_inspector.h"
#include "pnga/ui/qt/compression_context.h"
#include "pnga/ui/qt/decode_trace_inspector.h"
#include "pnga/ui/qt/huffman_inspector.h"

#include <QString>
#include <QStringList>

#include <cstdint>

namespace pnga::ui::qt {

namespace {

const char* lifecycle_key(
    pnga::analysis_engine::TraceInspectorLifecycle status) noexcept {
  switch (status) {
    case pnga::analysis_engine::TraceInspectorLifecycle::kEmpty:
      return "empty";
    case pnga::analysis_engine::TraceInspectorLifecycle::kLoading:
      return "loading";
    case pnga::analysis_engine::TraceInspectorLifecycle::kReplaying:
      return "replaying";
    case pnga::analysis_engine::TraceInspectorLifecycle::kReady:
      return "ready";
    case pnga::analysis_engine::TraceInspectorLifecycle::kPartial:
      return "partial";
    case pnga::analysis_engine::TraceInspectorLifecycle::kError:
      return "error";
    case pnga::analysis_engine::TraceInspectorLifecycle::kCancelled:
      return "cancelled";
    case pnga::analysis_engine::TraceInspectorLifecycle::kStaleGeneration:
      return "stale generation";
  }
  return "unknown";
}

}  // namespace

TraceInspectorBinding::TraceInspectorBinding(BlockInspector* block,
                                             HuffmanInspector* huffman,
                                             DecodeTraceInspector* decode,
                                             QObject* parent)
    : QObject(parent), block_(block), huffman_(huffman), decode_(decode) {}

void TraceInspectorBinding::publishFastIndex(
    const pnga::analysis_engine::FastCompressionIndexView& view) {
  if (block_ != nullptr) {
    block_->setFastIndex(view);
  }
}

void TraceInspectorBinding::publish(
    const pnga::analysis_engine::TraceQueryResult& result,
    std::optional<std::uint64_t> selected_token_index,
    std::optional<std::uint64_t> selected_output_offset,
    std::optional<std::uint64_t> scanline) {
  const auto bundle = pnga::analysis_engine::build_trace_inspector_bundle(
      result, selected_token_index, selected_output_offset, scanline);
  generation_ = bundle.generation;
  last_state_.generation = bundle.generation;
  last_state_.error = result.error;
  last_state_.bundle = bundle;
  switch (result.status) {
    case pnga::analysis_engine::TraceQueryStatus::kReady:
      last_state_.status = pnga::analysis_engine::TraceInspectorLifecycle::kReady;
      break;
    case pnga::analysis_engine::TraceQueryStatus::kPartial:
    case pnga::analysis_engine::TraceQueryStatus::kCancelled:
      last_state_.status =
          pnga::analysis_engine::TraceInspectorLifecycle::kPartial;
      break;
    case pnga::analysis_engine::TraceQueryStatus::kError:
      last_state_.status = pnga::analysis_engine::TraceInspectorLifecycle::kError;
      break;
    case pnga::analysis_engine::TraceQueryStatus::kNotIndexed:
    case pnga::analysis_engine::TraceQueryStatus::kReplaying:
      last_state_.status =
          pnga::analysis_engine::TraceInspectorLifecycle::kReplaying;
      break;
  }
  if (block_ != nullptr) {
    block_->setView(bundle.block);
  }
  if (huffman_ != nullptr) {
    huffman_->setView(bundle.huffman);
  }
  if (decode_ != nullptr) {
    decode_->setView(bundle.decode);
  }
  updateContext();
  emit generationPublished(static_cast<quint64>(generation_));
}

void TraceInspectorBinding::publishState(
    const pnga::analysis_engine::TraceInspectorState& state) {
  generation_ = state.generation;
  last_state_ = state;
  if (state.bundle.has_value()) {
    if (block_ != nullptr) {
      block_->setView(state.bundle->block);
    }
    if (huffman_ != nullptr) {
      huffman_->setView(state.bundle->huffman);
    }
    if (decode_ != nullptr) {
      decode_->setView(state.bundle->decode);
    }
  }
  updateContext();
  emit generationPublished(static_cast<quint64>(generation_));
}

void TraceInspectorBinding::clear() {
  generation_ = 0;
  not_indexed_ = false;
  last_state_ = pnga::analysis_engine::TraceInspectorState{};
  last_state_.generation = 0;
  if (block_ != nullptr) {
    block_->clear();
  }
  if (huffman_ != nullptr) {
    huffman_->clear();
  }
  if (decode_ != nullptr) {
    decode_->clear();
  }
  updateContext();
}

void TraceInspectorBinding::setContext(CompressionContext* context) {
  context_ = context;
  updateContext();
}

void TraceInspectorBinding::setHasDocument(bool has_document) {
  has_document_ = has_document;
  updateContext();
}

void TraceInspectorBinding::setNotIndexed(bool not_indexed) {
  not_indexed_ = not_indexed;
  updateContext();
}

void TraceInspectorBinding::updateContext() {
  if (context_ == nullptr) {
    return;
  }
  const QString instruction = QStringLiteral(
      "Select and lock a pixel to inspect its bounded DEFLATE provenance.");
  const QString open = QStringLiteral(
      "Open a PNG to inspect its compressed IDAT stream.");

  if (not_indexed_) {
    context_->setStatusText(QStringLiteral(
        "DEFLATE provenance is not indexed for this selection."));
    context_->setMappingText(has_document_ ? instruction : QString());
    return;
  }

  QString status;
  switch (last_state_.status) {
    case pnga::analysis_engine::TraceInspectorLifecycle::kEmpty:
    case pnga::analysis_engine::TraceInspectorLifecycle::kLoading:
      status = has_document_ ? instruction : open;
      break;
    case pnga::analysis_engine::TraceInspectorLifecycle::kReplaying:
      status = QStringLiteral("Replaying the selected output range…");
      break;
    case pnga::analysis_engine::TraceInspectorLifecycle::kReady: {
      status = QStringLiteral("Trace ready · generation %1")
                   .arg(static_cast<qulonglong>(last_state_.generation));
      if (last_state_.bundle.has_value()) {
        status += QStringLiteral(" · %1 associated blocks · %2 tokens in result")
                      .arg(static_cast<qulonglong>(
                          last_state_.bundle->block.rows.size()))
                      .arg(static_cast<qulonglong>(
                          last_state_.bundle->decode.steps.size()));
      }
      break;
    }
    case pnga::analysis_engine::TraceInspectorLifecycle::kPartial:
      status = QStringLiteral("Partial trace · verified rows are shown");
      if (!last_state_.error.empty()) {
        status += QStringLiteral(" · %1")
                      .arg(QString::fromStdString(last_state_.error));
      }
      break;
    case pnga::analysis_engine::TraceInspectorLifecycle::kError:
      status = last_state_.error.empty()
                   ? QStringLiteral("Trace stopped.")
                   : QStringLiteral("Trace stopped: %1")
                         .arg(QString::fromStdString(last_state_.error));
      break;
    case pnga::analysis_engine::TraceInspectorLifecycle::kCancelled:
    case pnga::analysis_engine::TraceInspectorLifecycle::kStaleGeneration:
      status = QStringLiteral("Trace request cancelled.");
      break;
  }
  context_->setStatusText(status);

  QString mapping;
  if (last_state_.bundle.has_value()) {
    const auto& block_view = last_state_.bundle->block;
    const auto& decode_view = last_state_.bundle->decode;
    QStringList parts;
    if (block_view.scanline.has_value()) {
      parts << QStringLiteral("scanline %1")
                   .arg(static_cast<qulonglong>(*block_view.scanline));
    }
    if (block_view.selected_output_offset.has_value()) {
      parts << QStringLiteral("output byte %1")
                   .arg(static_cast<qulonglong>(
                       *block_view.selected_output_offset));
    }
    if (block_view.selected_block_index.has_value()) {
      parts << QStringLiteral("Block #%1")
                   .arg(static_cast<qulonglong>(
                       *block_view.selected_block_index));
    }
    if (decode_view.selected_token_index.has_value()) {
      parts << QStringLiteral("Token #%1")
                   .arg(static_cast<qulonglong>(
                       *decode_view.selected_token_index));
    }
    if (!parts.isEmpty()) {
      mapping = QStringLiteral("Current · ") + parts.join(QStringLiteral(" · "));
    }
  }
  if (mapping.isEmpty()) {
    mapping = has_document_ ? instruction : QString();
  }
  context_->setMappingText(mapping);
}

}  // namespace pnga::ui::qt
