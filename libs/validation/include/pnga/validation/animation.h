#ifndef PNGA_VALIDATION_ANIMATION_H
#define PNGA_VALIDATION_ANIMATION_H

#include <pnga/png-format/animation_index.h>
#include <pnga/validation/structural.h>

namespace pnga::validation {

ValidationReport validate_animation(
    const pnga::png_format::AnimationIndex& index);

}  // namespace pnga::validation

#endif  // PNGA_VALIDATION_ANIMATION_H
