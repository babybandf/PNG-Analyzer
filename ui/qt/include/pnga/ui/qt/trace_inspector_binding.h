#ifndef PNGA_UI_QT_TRACE_INSPECTOR_BINDING_H
#define PNGA_UI_QT_TRACE_INSPECTOR_BINDING_H

// M5 Trace Gate: binds one immutable TraceQueryResult to the three existing
// inspector pages. The binding is intentionally explicit so a queued worker
// callback can publish one generation atomically at the Qt boundary. WP-5U12:
// the binding also drives the single shared Compression context above the
// page stack, so the trace state is not repeated on every page.

#include <pnga/analysis-engine/trace_inspector_bundle.h>
#include <pnga/analysis-engine/trace_inspector_state.h>

#include <QObject>

#include <cstdint>
#include <memory>
#include <optional>

namespace pnga::ui::qt {

class BlockInspector;
class CompressionContext;
class DecodeTraceInspector;
class HuffmanInspector;

class TraceInspectorBinding final : public QObject {
  Q_OBJECT
 public:
  TraceInspectorBinding(BlockInspector* block, HuffmanInspector* huffman,
                        DecodeTraceInspector* decode,
                        QObject* parent = nullptr);

  void publish(const pnga::analysis_engine::TraceQueryResult& result,
               std::optional<std::uint64_t> selected_token_index = std::nullopt,
               std::optional<std::uint64_t> selected_output_offset =
                   std::nullopt,
               std::optional<std::uint64_t> scanline = std::nullopt);
  void publishFastIndex(
      const pnga::analysis_engine::FastCompressionIndexView& view);
  void publishState(const pnga::analysis_engine::TraceInspectorState& state);
  void clear();
  std::uint64_t generation() const noexcept { return generation_; }

  // WP-5U12: attach the shared Compression context and feed it as part of the
  // same publication boundary.
  void setContext(CompressionContext* context);
  void setHasDocument(bool has_document);
  void setNotIndexed(bool not_indexed);

 signals:
  void generationPublished(quint64 generation);

 private:
  void updateContext();

  BlockInspector* block_ = nullptr;
  HuffmanInspector* huffman_ = nullptr;
  DecodeTraceInspector* decode_ = nullptr;
  CompressionContext* context_ = nullptr;
  pnga::analysis_engine::TraceInspectorState last_state_;
  bool has_document_ = false;
  bool not_indexed_ = false;
  std::uint64_t generation_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_TRACE_INSPECTOR_BINDING_H
