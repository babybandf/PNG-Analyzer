// M5 Trace Gate Qt boundary binding.

#include "pnga/ui/qt/trace_inspector_binding.h"

#include "pnga/ui/qt/block_inspector.h"
#include "pnga/ui/qt/decode_trace_inspector.h"
#include "pnga/ui/qt/huffman_inspector.h"

#include <QString>

namespace pnga::ui::qt {

TraceInspectorBinding::TraceInspectorBinding(BlockInspector* block,
                                             HuffmanInspector* huffman,
                                             DecodeTraceInspector* decode,
                                             QObject* parent)
    : QObject(parent), block_(block), huffman_(huffman), decode_(decode) {}

void TraceInspectorBinding::publish(
    const pnga::analysis_engine::TraceQueryResult& result,
    std::optional<std::uint64_t> selected_token_index,
    std::optional<std::uint64_t> selected_output_offset,
    std::optional<std::uint64_t> scanline) {
  const auto bundle = pnga::analysis_engine::build_trace_inspector_bundle(
      result, selected_token_index, selected_output_offset, scanline);
  generation_ = bundle.generation;
  if (block_ != nullptr) {
    block_->setView(bundle.block);
  }
  if (huffman_ != nullptr) {
    huffman_->setView(bundle.huffman);
  }
  if (decode_ != nullptr) {
    decode_->setView(bundle.decode);
  }
  emit generationPublished(static_cast<quint64>(generation_));
}

void TraceInspectorBinding::publishState(
    const pnga::analysis_engine::TraceInspectorState& state) {
  generation_ = state.generation;
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
  const QString status = QStringLiteral("Trace: %1 (generation %2)%3")
                             .arg(QLatin1String(
                                 pnga::analysis_engine::
                                     trace_inspector_lifecycle_text(
                                         state.status)))
                             .arg(static_cast<qulonglong>(state.generation))
                             .arg(state.error.empty()
                                      ? QString{}
                                      : QStringLiteral(" — %1")
                                            .arg(QString::fromStdString(
                                                state.error)));
  if (block_ != nullptr) {
    block_->setExternalStatus(status);
  }
  if (huffman_ != nullptr) {
    huffman_->setExternalStatus(status);
  }
  if (decode_ != nullptr) {
    decode_->setExternalStatus(status);
  }
  emit generationPublished(static_cast<quint64>(generation_));
}

void TraceInspectorBinding::clear() {
  generation_ = 0;
  if (block_ != nullptr) {
    block_->clear();
  }
  if (huffman_ != nullptr) {
    huffman_->clear();
  }
  if (decode_ != nullptr) {
    decode_->clear();
  }
}

}  // namespace pnga::ui::qt
