#ifndef PNGA_UI_QT_TRACE_INSPECTOR_BINDING_H
#define PNGA_UI_QT_TRACE_INSPECTOR_BINDING_H

// M5 Trace Gate: binds one immutable TraceQueryResult to the three existing
// inspector pages. The binding is intentionally explicit so a queued worker
// callback can publish one generation atomically at the Qt boundary.

#include <pnga/analysis-engine/trace_inspector_bundle.h>

#include <QObject>

#include <cstdint>
#include <memory>
#include <optional>

namespace pnga::ui::qt {

class BlockInspector;
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
  void clear();
  std::uint64_t generation() const noexcept { return generation_; }

 signals:
  void generationPublished(quint64 generation);

 private:
  BlockInspector* block_ = nullptr;
  HuffmanInspector* huffman_ = nullptr;
  DecodeTraceInspector* decode_ = nullptr;
  std::uint64_t generation_ = 0;
};

}  // namespace pnga::ui::qt

#endif  // PNGA_UI_QT_TRACE_INSPECTOR_BINDING_H
