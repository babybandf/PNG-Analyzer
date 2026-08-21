#ifndef PNGA_ANALYSIS_ENGINE_STAGE_ANALYSIS_H
#define PNGA_ANALYSIS_ENGINE_STAGE_ANALYSIS_H

// WP-306: stage analysis — materialize the Filtered / Unfiltered / Native
// stage data the stage inspector displays, and replay an on-demand per-byte
// filter formula for a chosen scanline. Qt-free (ADR-0003); the GUI consumes
// these immutable results and never runs the pipeline itself.

#include <pnga/io/byte_source.h>
#include <pnga/png-format/virtual_idat_stream.h>
#include <pnga/png-reconstruction/native_samples.h>
#include <pnga/png-reconstruction/pass_reconstruction.h>
#include <pnga/png-reconstruction/reverse_filter.h>
#include <pnga/png-reconstruction/scanline_layout.h>

#include "pnga/analysis-engine/filtered_scanlines.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pnga::analysis_engine {

// All stage data for one image, ready for the inspector. `filtered` holds the
// flat filtered bytes; `unfiltered` the packed reconstructed target;
// `native` the canonical native samples; `passes` the per-pass unfiltered rows
// (used by the on-demand filter formula); `scanlines` the stream-order spans.
struct StageSet {
  bool success = false;
  std::string error;
  pnga::png_reconstruction::ImageHeader header;
  bool interlace = false;
  std::vector<FilteredScanlineSpan> scanlines;  // stream order (pass-major)
  std::vector<pnga::png_reconstruction::ReconstructedPass> passes;
  std::vector<std::byte> filtered;    // flat filtered scanline bytes
  std::vector<std::byte> unfiltered;  // packed target (height * row_bytes)
  pnga::png_reconstruction::NativeImage native;  // canonical native samples
};

// Runs the reconstruction pipeline and materializes every stage. `stream` must
// be the virtual IDAT stream of `source` and `header` the image's IHDR fields.
StageSet analyze_stages(
    const pnga::png_format::VirtualIDATStream& stream,
    const pnga::io::IByteSource& source,
    const pnga::png_reconstruction::ImageHeader& header);

// Convenience entry for the GUI: parses the signature and IHDR, builds the
// virtual IDAT stream and materializes every stage in one call.
StageSet analyze_source(const pnga::io::IByteSource& source);

// On-demand per-byte filter formula for one scanline (index into
// `set.scanlines`, stream order). The inspector uses this to show the
// a/b/c/predictor/recon values for a selected byte. One FilterTraceEvent is
// produced per data byte of the scanline.
struct FilterFormula {
  bool success = false;
  std::string error;
  std::uint64_t row = 0;
  pnga::png_reconstruction::FilterType filter =
      pnga::png_reconstruction::FilterType::kNone;
  std::vector<pnga::png_reconstruction::FilterTraceEvent> events;
};

FilterFormula filter_formula(const StageSet& set, std::uint64_t row);

}  // namespace pnga::analysis_engine

#endif  // PNGA_ANALYSIS_ENGINE_STAGE_ANALYSIS_H
