#pragma once
// Path verbs: combine/break, divide/cut, stroke-to-path, inset/outset,
// dynamic offset, simplify.
//
// Booleans map to union/difference/intersection/exclusion/division/cut,
// stroke-to-path outlines a stroke, the offset family covers inset/outset/
// dynamic/linked offset, and simplify is Douglas-Peucker with a threshold.
// Built on the existing boolean kernel + flattenSegments so results stay
// editable Segment lists.
#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector {

// Concatenate segment lists (combine); split into connected components
// (break apart). Round-trips through Segment so handles survive.
std::vector<Segment> combinePaths(const std::vector<std::vector<Segment>>& parts);
std::vector<std::vector<Segment>> breakApart(const std::vector<Segment>& segs);

// Division: split subject by clip into (inside, outside) fragment sets.
// Cut: split along the clip polyline (open clip) with `gap` width.
struct DivideResult {
    std::vector<std::vector<Segment>> inside;
    std::vector<std::vector<Segment>> outside;
};
DivideResult dividePath(const std::vector<Segment>& subject,
                        const std::vector<Segment>& clip, float tolerance = 0.5f);
std::vector<std::vector<Segment>> cutPath(const std::vector<Segment>& subject,
                                           const std::vector<Segment>& cutter,
                                           float tolerance = 0.5f);

// Stroke-to-path: expand `segs` with `style` into filled outline segments.
std::vector<Segment> strokeToSegments(const std::vector<Segment>& segs,
                                      const StrokeStyle& style, float tolerance = 0.5f);

// Inset/outset by `amount` (positive = outset). Miter handling mirrors the
// stroker; curves flatten at `tolerance`.
std::vector<Segment> offsetPath(const std::vector<Segment>& segs, double amount,
                                float tolerance = 0.5f);

// Dynamic offset: single-radius variant with `corner` rounding preserved as
// cubic spans (linked offset shares this with a live source id, resolved in
// UI code).
std::vector<Segment> dynamicOffset(const std::vector<Segment>& segs, double radius,
                                   float tolerance = 0.5f);

// Simplify with threshold (Ramer-Douglas-Peucker on the flattened form,
// refit to lines; curves re-smooth when `smooth` is set).
std::vector<Segment> simplifyPath(const std::vector<Segment>& segs, double threshold,
                                  bool smooth = true, float tolerance = 0.5f);

// Fillet/chamfer every corner to radius `r` (chamfer when `chamfer=true).
std::vector<Segment> filletPath(const std::vector<Segment>& segs, double r,
                                bool chamfer = false);

}  // namespace pittore::vector
