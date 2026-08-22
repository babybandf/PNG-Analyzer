// M5 Trace Gate Qt boundary binding.

#include "pnga/ui/qt/trace_inspector_binding.h"

#include "pnga/ui/qt/block_inspector.h"
#include "pnga/ui/qt/decode_trace_inspector.h"
#include "pnga/ui/qt/huffman_inspector.h"

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
