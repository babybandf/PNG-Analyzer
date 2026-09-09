#include "pnga/analysis-engine/canvas_pixel_query.h"

#include <pnga/png-reconstruction/canvas_composition.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace pnga::analysis_engine {
namespace {

// Fixed page budgets (contract C5).
constexpr std::size_t kMaxNodes = 4096;
constexpr std::uint64_t kMaxHistorySteps = 1024;
constexpr std::size_t kMaxPending = 1024;
constexpr std::size_t kMaxNodeBytes = 4u << 20;
// Conservative per-node footprint for the memory bound (node payload plus
// a two-entry inputs vector including heap slack).
constexpr std::size_t kNodeFootprintBytes = 128;

bool legal_canvas_stage(pnga::trace_model::Stage stage) noexcept {
  switch (stage) {
    case pnga::trace_model::Stage::kFrameOutput:
    case pnga::trace_model::Stage::kPreBlend:
    case pnga::trace_model::Stage::kPostBlend:
    case pnga::trace_model::Stage::kPostDispose:
      return true;
    default:
      return false;
  }
}

std::optional<std::uint32_t> ticket_frame(
    const pnga::trace_model::InspectionTicket& ticket) {
  const auto* frame =
      std::get_if<pnga::trace_model::AnimationFrame>(&ticket.key.identity);
  if (frame == nullptr) {
    return std::nullopt;
  }
  return frame->index;
}

struct WorkState {
  std::uint32_t frame = 0;
  pnga::trace_model::Stage stage = pnga::trace_model::Stage::kUnknown;

  bool operator==(const WorkState&) const = default;
};

// One expansion of a state: a concrete node (leaves carry their rgba;
// derived nodes resolve it from their children at emit time), a same-page
// alias (continue with the single child, no node), or an error.
struct Expansion {
  enum class Kind { kNode, kAlias, kError };

  Kind kind = Kind::kError;
  // Children in deterministic depth-first order: the current frame's
  // source first, then the destination history.
  std::vector<WorkState> children;
  CanvasPixelNode node;
  const char* error = nullptr;
};

class CanvasPixelQuery {
 public:
  CanvasPixelQuery(const CanvasPixelRequest& request,
                   const CancellationToken* cancellation)
      : request_(request), cancellation_(cancellation) {}

  CanvasPixelResult run(std::vector<WorkState> initial_work) {
    CanvasPixelResult result;
    std::vector<WorkState> work = std::move(initial_work);
    std::vector<WorkState> in_flight;
    std::vector<Parked> parked;

    const auto stop_with = [&](CanvasPixelResult::Stop stop,
                               std::optional<CanvasPixelCursor> next,
                               const char* error) {
      result.stop = stop;
      result.error = error != nullptr ? error : "";
      result.next = std::move(next);
      result.nodes = std::move(nodes_);
      return result;
    };

    while (true) {
      if (cancelled()) {
        return stop_with(CanvasPixelResult::Stop::kCancelled, std::nullopt,
                         "canvas pixel query cancelled");
      }
      if (work.empty()) {
        break;  // the page closed with every dependency resolved
      }
      // Budget checks happen before the next expansion. A work stack beyond
      // the frozen pending cap also paginates (the cursor never carries
      // more than kMaxPending entries).
      if (nodes_.size() >= kMaxNodes || history_steps_ >= kMaxHistorySteps ||
          node_bytes_ + kNodeFootprintBytes > kMaxNodeBytes ||
          work.size() > kMaxPending ||
          visited_steps_ == std::numeric_limits<std::uint64_t>::max()) {
        // Cut every unexpanded state into a carry leaf so this page stays
        // a closed DAG, and carry the states over in deterministic
        // depth-first order (the next state to expand first).
        CanvasPixelCursor cursor;
        cursor.version = 1;
        cursor.ticket = request_.ticket;
        cursor.x = request_.x;
        cursor.y = request_.y;
        cursor.visited_steps = visited_steps_;
        for (auto state = work.rbegin(); state != work.rend(); ++state) {
          emit_leaf_carry(*state, &parked);
          cursor.pending.push_back(
              CanvasPixelPending{state->frame, state->stage});
        }
        return stop_with(CanvasPixelResult::Stop::kPartial,
                         std::move(cursor), nullptr);
      }

      const WorkState state = work.back();
      work.pop_back();
      const Expansion expansion = expand(state);
      if (expansion.kind == Expansion::Kind::kError) {
        return stop_with(CanvasPixelResult::Stop::kError, std::nullopt,
                         expansion.error);
      }
      if (expansion.kind == Expansion::Kind::kAlias) {
        ++visited_steps_;
        // Aliases move to strictly earlier frames or earlier stages of the
        // same frame; a frame crossing counts against the history budget.
        const WorkState& target = expansion.children.front();
        if (target.frame < state.frame) {
          ++history_steps_;
        }
        if (std::find(in_flight.begin(), in_flight.end(), target) !=
            in_flight.end()) {
          return stop_with(CanvasPixelResult::Stop::kError, std::nullopt,
                           "canvas provenance self-loop");
        }
        work.push_back(target);
      } else if (expansion.children.empty()) {
        ++visited_steps_;
        emit_node(std::move(expansion.node), &parked);
      } else {
        ++visited_steps_;
        Parked frame;
        frame.state = state;
        frame.node = expansion.node;
        frame.remaining = expansion.children.size();
        frame.in_flight_depth = in_flight.size();
        parked.push_back(frame);
        node_bytes_ += kNodeFootprintBytes;
        in_flight.push_back(state);
        // Push reversed so the first child (source) expands and emits
        // first with the smaller node index.
        for (auto child = expansion.children.rbegin();
             child != expansion.children.rend(); ++child) {
          if (std::find(in_flight.begin(), in_flight.end(), *child) !=
              in_flight.end()) {
            return stop_with(CanvasPixelResult::Stop::kError, std::nullopt,
                             "canvas provenance self-loop");
          }
          work.push_back(*child);
        }
      }

    }
    if (!parked.empty()) {
      return stop_with(CanvasPixelResult::Stop::kError, std::nullopt,
                       "canvas provenance ended with unresolved dependencies");
    }
    return stop_with(CanvasPixelResult::Stop::kReady, std::nullopt, nullptr);
  }

 private:
  struct Parked {
    WorkState state;
    CanvasPixelNode node;       // inputs and derived rgba filled at emit
    std::size_t remaining = 0;  // children not yet emitted
    std::size_t child_count = 0;
    std::size_t emitted = 0;    // children emitted so far (slot order)
    std::array<std::uint32_t, 2> child_indices{};
    std::size_t in_flight_depth = 0;
  };

  bool cancelled() const noexcept {
    return cancellation_ != nullptr && cancellation_->cancelled();
  }

  // Emits a leaf node and notifies the deepest parked frame.
  void emit_node(CanvasPixelNode node, std::vector<Parked>* parked) {
    const std::uint32_t index = static_cast<std::uint32_t>(nodes_.size());
    node_bytes_ += kNodeFootprintBytes;
    nodes_.push_back(std::move(node));
    attach_and_drain(index, parked);
  }

  void emit_leaf_carry(const WorkState& state, std::vector<Parked>* parked) {
    CanvasPixelNode node;
    node.operation = CanvasOperation::kCarry;
    node.frame = state.frame;
    node.stage = state.stage;
    node.rgba = {0, 0, 0, 0};  // provisional: partial pages do not claim
                               // complete provenance values
    emit_node(std::move(node), parked);
  }

  // Records `emitted_index` as the next child slot of the deepest parked
  // frame; when that frame's children are complete it emits and notifies
  // its own parent in turn.
  void attach_and_drain(std::uint32_t emitted_index,
                        std::vector<Parked>* parked) {
    if (parked->empty()) {
      return;
    }
    Parked& parent = parked->back();
    parent.child_indices[parent.emitted++] = emitted_index;
    if (--parent.remaining != 0) {
      return;
    }
    Parked done = std::move(parked->back());
    parked->pop_back();
    CanvasPixelNode node = std::move(done.node);
    for (std::size_t i = 0; i < done.emitted; ++i) {
      node.inputs.push_back(done.child_indices[i]);
    }
    // Derived values: blend applies the compositor formula; carry and
    // restore adopt the carried (single input) value.
    if (node.inputs.size() == 2) {
      std::array<std::uint8_t, 4> blended;
      if (blend_pixel(nodes_[node.inputs[0]].rgba,
                      nodes_[node.inputs[1]].rgba,
                      node.operation == CanvasOperation::kBlendOver,
                      &blended)) {
        node.rgba = blended;
      } else {
        node.rgba = {0, 0, 0, 0};
      }
    } else if (node.inputs.size() == 1) {
      node.rgba = nodes_[node.inputs[0]].rgba;
    }
    node_bytes_ += kNodeFootprintBytes;
    const std::uint32_t index = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back(std::move(node));
    attach_and_drain(index, parked);
  }

  bool inside_rect(std::uint32_t frame) const noexcept {
    const auto& control = request_.document.index->frames[frame].control;
    const std::uint64_t rx = control.x;
    const std::uint64_t ry = control.y;
    return request_.x >= rx && request_.x < rx + control.width &&
           request_.y >= ry && request_.y < ry + control.height;
  }

  // Delivered RGBA pixel of `frame` at the canvas pixel (frame-local).
  bool frame_pixel(std::uint32_t frame, std::array<std::uint8_t, 4>* rgba,
                   const char** error) {
    for (const auto& cached : frame_cache_) {
      if (cached.first == frame) {
        *rgba = cached.second;
        return true;
      }
    }
    FrameRequest frame_request = request_.document;
    frame_request.ordinal = frame;
    const FrameResult analyzed = analyze_frame(frame_request, cancellation_);
    if (cancelled()) {
      *error = "canvas pixel query cancelled";
      return false;
    }
    if (analyzed.stop != FrameResult::Stop::kReady || !analyzed.frame) {
      *error = analyzed.error.empty() ? "frame analysis failed"
                                      : analyzed.error.c_str();
      return false;
    }
    const auto& image = analyzed.frame->delivered;
    const auto& control = request_.document.index->frames[frame].control;
    const std::uint64_t lx = request_.x - control.x;
    const std::uint64_t ly = request_.y - control.y;
    if (lx >= image.width || ly >= image.height ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * image.height * 4) {
      *error = "frame delivered image does not cover the frame rectangle";
      return false;
    }
    const std::size_t offset =
        (static_cast<std::size_t>(ly) * image.width + lx) * 4;
    std::array<std::uint8_t, 4> value{image.pixels[offset],
                                      image.pixels[offset + 1],
                                      image.pixels[offset + 2],
                                      image.pixels[offset + 3]};
    if (frame_cache_.size() < 64) {
      frame_cache_.emplace_back(frame, value);
    }
    *rgba = value;
    return true;
  }

  // The compositor's integer blend for one pixel through the production
  // blend_into on 1x1 images (bit-identical to the full-rect path).
  bool blend_pixel(std::array<std::uint8_t, 4> source,
                   std::array<std::uint8_t, 4> destination, bool over,
                   std::array<std::uint8_t, 4>* out) const {
    pnga::png_reconstruction::RgbaImage canvas{
        1, 1,
        {destination[0], destination[1], destination[2], destination[3]}};
    pnga::png_reconstruction::RgbaImage frame{
        1, 1, {source[0], source[1], source[2], source[3]}};
    const auto result = pnga::png_reconstruction::blend_into(
        canvas, frame, pnga::png_reconstruction::FrameRect{0, 0, 1, 1},
        over ? pnga::png_reconstruction::Blend::kOver
             : pnga::png_reconstruction::Blend::kSource,
        [] { return false; });
    if (!result.success) {
      return false;
    }
    *out = {canvas.pixels[0], canvas.pixels[1], canvas.pixels[2],
            canvas.pixels[3]};
    return true;
  }

  Expansion expand(const WorkState& state) {
    Expansion expansion;
    const auto& control = request_.document.index->frames[state.frame].control;
    CanvasPixelNode& node = expansion.node;
    node.frame = state.frame;
    node.stage = state.stage;

    switch (state.stage) {
      case pnga::trace_model::Stage::kPreBlend: {
        // PreBlend(0) is the initial transparent canvas; PreBlend(N>0) is
        // the post-dispose canvas of frame N-1 (alias, no node).
        if (state.frame == 0) {
          node.operation = CanvasOperation::kClear;
          node.rgba = {0, 0, 0, 0};
          expansion.kind = Expansion::Kind::kNode;
          return expansion;
        }
        expansion.kind = Expansion::Kind::kAlias;
        expansion.children.push_back(
            WorkState{state.frame - 1,
                      pnga::trace_model::Stage::kPostDispose});
        return expansion;
      }
      case pnga::trace_model::Stage::kPostDispose: {
        if (!inside_rect(state.frame)) {
          // Disposal only touches the frame rectangle.
          expansion.kind = Expansion::Kind::kAlias;
          expansion.children.push_back(
              WorkState{state.frame, pnga::trace_model::Stage::kPostBlend});
          return expansion;
        }
        if (control.dispose == 1) {
          node.operation = CanvasOperation::kClear;
          node.rgba = {0, 0, 0, 0};
          expansion.kind = Expansion::Kind::kNode;
          return expansion;
        }
        if (control.dispose == 2) {
          // Dispose-to-previous restores this frame's saved PreBlend rect.
          node.operation = CanvasOperation::kRestore;
          expansion.children.push_back(
              WorkState{state.frame, pnga::trace_model::Stage::kPreBlend});
          expansion.kind = Expansion::Kind::kNode;
          return expansion;
        }
        // dispose == 0 (kNone): the post-dispose canvas keeps the
        // post-blend pixels (alias, no node).
        expansion.kind = Expansion::Kind::kAlias;
        expansion.children.push_back(
            WorkState{state.frame, pnga::trace_model::Stage::kPostBlend});
        return expansion;
      }
      case pnga::trace_model::Stage::kPostBlend: {
        if (!inside_rect(state.frame)) {
          // Outside the rectangle the blend does not touch the pixel: the
          // value carries over from this frame's PreBlend.
          node.operation = CanvasOperation::kCarry;
          expansion.children.push_back(
              WorkState{state.frame, pnga::trace_model::Stage::kPreBlend});
          expansion.kind = Expansion::Kind::kNode;
          return expansion;
        }
        node.operation = control.blend == 0 ? CanvasOperation::kBlendSource
                                            : CanvasOperation::kBlendOver;
        // Source first, destination history second (deterministic order).
        expansion.children.push_back(
            WorkState{state.frame, pnga::trace_model::Stage::kFrameOutput});
        expansion.children.push_back(
            WorkState{state.frame, pnga::trace_model::Stage::kPreBlend});
        expansion.kind = Expansion::Kind::kNode;
        return expansion;
      }
      case pnga::trace_model::Stage::kFrameOutput: {
        if (!inside_rect(state.frame)) {
          // The frame has no pixel here: a carry leaf with no
          // contribution.
          node.operation = CanvasOperation::kCarry;
          node.rgba = {0, 0, 0, 0};
          expansion.kind = Expansion::Kind::kNode;
          return expansion;
        }
        node.operation = CanvasOperation::kFrameSample;
        const char* error = nullptr;
        std::array<std::uint8_t, 4> rgba;
        if (!frame_pixel(state.frame, &rgba, &error)) {
          expansion.kind = Expansion::Kind::kError;
          expansion.error = error;
          return expansion;
        }
        node.rgba = rgba;
        expansion.kind = Expansion::Kind::kNode;
        return expansion;
      }
      default:
        expansion.kind = Expansion::Kind::kError;
        expansion.error = "canvas query reached an illegal stage";
        return expansion;
    }
  }

  const CanvasPixelRequest& request_;
  const CancellationToken* cancellation_;
  std::vector<CanvasPixelNode> nodes_;
  std::vector<std::pair<std::uint32_t, std::array<std::uint8_t, 4>>>
      frame_cache_;
  std::uint64_t visited_steps_ = 0;
  std::uint64_t history_steps_ = 0;
  std::size_t node_bytes_ = 0;
};

}  // namespace

CanvasPixelResult query_canvas_pixel(const CanvasPixelRequest& request,
                                     const CancellationToken* cancellation) {
  CanvasPixelResult result;
  const auto cancelled = [cancellation]() {
    return cancellation != nullptr && cancellation->cancelled();
  };
  if (cancelled()) {
    result.stop = CanvasPixelResult::Stop::kCancelled;
    result.error = "canvas pixel query cancelled";
    return result;
  }
  if (!request.document.source || !request.document.index) {
    result.error = "canvas pixel query requires the document source and index";
    return result;
  }
  const auto ticket_frame_index = ticket_frame(request.ticket);
  if (!ticket_frame_index.has_value()) {
    result.error = "canvas pixel query requires an animation frame ticket";
    return result;
  }
  if (!legal_canvas_stage(request.ticket.stage)) {
    result.error = "canvas pixel query requires a canvas stage";
    return result;
  }
  const auto& canvas = request.document.canvas_header;
  if (request.x >= canvas.width || request.y >= canvas.height) {
    result.error = "canvas pixel is outside the canvas";
    return result;
  }
  if (*ticket_frame_index >= request.document.index->frames.size()) {
    result.error = "frame ordinal is outside the verified prefix";
    return result;
  }

  CanvasPixelQuery query(request, cancellation);
  if (request.cursor.has_value()) {
    const auto& cursor = *request.cursor;
    if (cursor.version != 1) {
      result.error = "canvas pixel cursor version mismatch";
      return result;
    }
    if (!(cursor.ticket == request.ticket)) {
      result.error = "canvas pixel cursor ticket mismatch";
      return result;
    }
    if (cursor.x != request.x || cursor.y != request.y) {
      result.error = "canvas pixel cursor coordinates mismatch";
      return result;
    }
    if (cursor.pending.empty()) {
      result.error = "canvas pixel cursor has an empty pending stack";
      return result;
    }
    if (cursor.pending.size() > kMaxPending) {
      result.error = "canvas pixel cursor pending stack exceeds the cap";
      return result;
    }
    for (const auto& pending : cursor.pending) {
      if (!legal_canvas_stage(pending.stage)) {
        result.error = "canvas pixel cursor carries an illegal stage";
        return result;
      }
      if (pending.frame >= request.document.index->frames.size() ||
          pending.frame > *ticket_frame_index) {
        result.error =
            "canvas pixel cursor carries a frame outside the verified prefix";
        return result;
      }
    }
    // The whole pending stack becomes the initial work stack: pending[0]
    // (the next state to expand) must end up on top.
    std::vector<WorkState> work;
    work.reserve(cursor.pending.size());
    for (auto state = cursor.pending.rbegin();
         state != cursor.pending.rend(); ++state) {
      work.push_back(WorkState{state->frame, state->stage});
    }
    return query.run(std::move(work));
  }

  return query.run({WorkState{*ticket_frame_index, request.ticket.stage}});
}

}  // namespace pnga::analysis_engine
