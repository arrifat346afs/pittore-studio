#pragma once
// Bitmap tracing: monochrome + color + centerline.
//
// A self-contained marching-squares tracer: threshold/quantize the tile,
// extract iso-contours, simplify, emit Segment loops. Centerline mode thins
// via Zhang-Suen and links skeleton pixels into polylines.
#include <cstdint>
#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector {

enum class TraceMode { Mono, Color, Centerline };

struct TraceSpec {
    TraceMode mode = TraceMode::Mono;
    double threshold = 0.5;   // mono cutoff on luminance
    int colors = 8;           // color mode: quantize levels per channel-ish
    double smooth = 1.0;      // contour smoothing (simplify threshold px)
    bool stackScans = true;   // color mode: one layer per quantum
    double minArea = 4.0;     // drop specks smaller than this (px^2)
};

// Luminance buffer trace: `lum` is w*h floats 0..1.
std::vector<std::vector<Segment>> traceBitmap(const std::vector<float>& lum, int w, int h,
                                              const TraceSpec& spec);

// RGBA trace: converts to luminance (mono/centerline) or quantizes (color).
std::vector<std::vector<Segment>> traceRgba(const std::vector<std::uint8_t>& rgba, int w,
                                            int h, const TraceSpec& spec);

}  // namespace pittore::vector
