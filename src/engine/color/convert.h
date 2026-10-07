#pragma once
// CMYK <-> RGB conversion core (clean-room implementation).
//
// Naive (profile-free) math only: the no-LCMS2 fallback and the import
// promoter. Profiled transforms — intents, black-point compensation, gamut
// alarms — belong to ProofManager (proof.h), which owns the lcms2
// dependency. Keeping the two apart leaves this file dependency-free and
// testable on every build.
//
// Convention: CMYK channels are ink coverage 0..1 (0 = no ink). RGB is
// straight (non-premultiplied) light 0..1 in the working space; alpha rides
// through untouched. Inputs outside 0..1 are clamped, never NaN.

#include "engine/core/pixel.h"

namespace pittore::color {

struct CmykF {
    float c = 0.0f;
    float m = 0.0f;
    float y = 0.0f;
    float k = 0.0f;
};

// Working-space RGB -> ink coverage (alpha dropped; coverage has none).
CmykF rgbToCmyk(RGBAf rgb) noexcept;
// Ink coverage -> working-space RGB (alpha supplied by the caller).
RGBAf cmykToRgb(CmykF ink, float alpha) noexcept;

}  // namespace pittore::color
