#include "pnga/validation/animation.h"

#include <cstdint>
#include <string>

namespace pnga::validation {

namespace {

std::string normalized_rule(const std::string& rule) {
  if (rule == "apng.envelope" || rule == "apng.frame.geometry" ||
      rule == "apng.frame.data") {
    return "apng.frame.data/geometry";
  }
  if (rule == "apng.actl.order" || rule == "apng.actl.count") {
    return "apng.actl.order/count";
  }
  return rule;
}

void add_issue(ValidationReport& report, const std::string& rule_id,
               const std::string& message, std::uint64_t offset) {
  report.issues.push_back(ValidationIssue{rule_id, Severity::kError, message,
                                          offset, "PNG:11.3.6"});
}

}  // namespace

ValidationReport validate_animation(
    const pnga::png_format::AnimationIndex& index) {
  ValidationReport report;
  for (const auto& issue : index.issues) {
    const auto rule = normalized_rule(issue.rule_id);
    add_issue(report, rule, rule, issue.offset);
  }

  if (index.status == pnga::png_format::AnimationStatus::kStatic) {
    return report;
  }
  if (index.stop == pnga::png_format::AnimationStop::kBudget) {
    add_issue(report, "apng.resource.limit", "animation resource limit",
              index.issues.empty() ? 0 : index.issues.back().offset);
    return report;
  }
  if (index.stop == pnga::png_format::AnimationStop::kCancelled) {
    add_issue(report, "apng.resource.limit", "animation scan cancelled", 0);
    return report;
  }

  if (!index.control.has_value() ||
      index.frames.size() != index.control->num_frames) {
    add_issue(report, "apng.frame.count", "animation frame count mismatch", 0);
  }
  for (std::size_t i = 0; i < index.frames.size(); ++i) {
    const auto& frame = index.frames[i];
    if (frame.control.dispose > 2 || frame.control.blend > 1) {
      add_issue(report, "apng.frame.control", "invalid frame control", 0);
    }
  }
  return report;
}

}  // namespace pnga::validation
