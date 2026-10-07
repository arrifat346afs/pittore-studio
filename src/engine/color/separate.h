#pragma once
// Appearance RGBA16 -> CMYK ink planes (clean-room glue for the CMYK
// writers). The documents stay RGB-internal: separation happens once, at
// write time, so the composite is flattened in appearance space first and
// the inks follow from it — the standard RGB-workflow order.
//
// Sample convention: ink coverage 0..65535 (0 = no ink), which is what
// TIFF's PHOTOMETRIC_SEPARATED wants. The PSD encoder inverts on emission
// itself (its spec stores the complement); keep this layer convention-free.
// `icc` selects the destination: a CMYK ICC profile gives the profiled
// relative-colorimetric + BPC separation, anything else falls back to the
// naive full-GCR core so an export never fails for lack of a profile.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pittore::color {

// `rgba` is interleaved straight-alpha RGBA16 (pixels * 4). Writes `out`
// as interleaved ink planes: pixels * 5 (C,M,Y,K,A) when `withAlpha`,
// otherwise pixels * 4 (C,M,Y,K). Alpha, when requested, rides through
// untouched — it is transparency, not an ink. Chunked internally so a
// huge export never holds more than a few scratch buffers.
void separateRgba16(const std::uint16_t* rgba, std::size_t pixels,
                    const std::uint8_t* icc, std::size_t iccLen,
                    bool withAlpha, std::vector<std::uint16_t>& out);

}  // namespace pittore::color
