// WP-5U15: QThread worker bodies moved verbatim from main_window.cpp. The
// four work calls are unchanged; all decoding/validation stays off the UI
// thread and stale results are gated by generation at the receiver.

#include "document_workers.h"

DecodeWorker::DecodeWorker(std::uint64_t generation,
                           std::shared_ptr<pnga::io::IByteSource> source,
                           QObject* parent)
    : QThread(parent),
      generation_(generation),
      source_(std::move(source)) {}

void DecodeWorker::run() {
  result_ = pnga::analysis_engine::decode_reference(*source_);
  emit decodeDone(generation_);
}

StageWorker::StageWorker(std::uint64_t generation,
                         std::shared_ptr<pnga::io::IByteSource> source,
                         QObject* parent)
    : QThread(parent), generation_(generation), source_(std::move(source)) {}

void StageWorker::run() {
  result_ = std::make_shared<pnga::analysis_engine::StageSet>(
      pnga::analysis_engine::analyze_source(*source_));
  emit stageDone(generation_);
}

ValidationWorker::ValidationWorker(
    std::uint64_t generation, std::shared_ptr<pnga::io::IByteSource> source,
    pnga::png_format::ChunkIndex index, QObject* parent)
    : QThread(parent),
      generation_(generation),
      source_(std::move(source)),
      index_(std::move(index)) {}

void ValidationWorker::run() {
  result_ = pnga::analysis_engine::validate_document(*source_, index_);
  emit validationDone(generation_);
}

AnimationIndexWorker::AnimationIndexWorker(
    std::uint64_t generation, std::shared_ptr<pnga::io::IByteSource> source,
    pnga::png_format::AnimationLimits limits, QObject* parent)
    : QThread(parent),
      generation_(generation),
      source_(std::move(source)),
      limits_(limits) {}

void AnimationIndexWorker::run() {
  if (source_ == nullptr) {
    result_ = std::make_shared<pnga::png_format::AnimationIndex>();
    result_->status = pnga::png_format::AnimationStatus::kInvalid;
  } else {
    result_ = std::make_shared<pnga::png_format::AnimationIndex>(
        pnga::png_format::index_animation(
            *source_, limits_, [this] { return cancelled_.load(); }));
  }
  emit animationDone(generation_);
}

ChunkDetailWorker::ChunkDetailWorker(
    std::uint64_t generation, std::uint64_t selection_serial,
    std::shared_ptr<pnga::io::IByteSource> source,
    pnga::png_format::ChunkNode node, QObject* parent)
    : QThread(parent),
      generation_(generation),
      selection_serial_(selection_serial),
      source_(std::move(source)),
      node_(node) {}

void ChunkDetailWorker::run() {
  result_ = pnga::png_format::describe_chunk(*source_, node_);
  emit detailDone(generation_, selection_serial_);
}
