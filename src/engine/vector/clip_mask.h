#pragma once
// Clip paths and masks: object-domain clipping.
//
// A clipPath is a list of shapes whose union clips; a mask is greyscale art
// whose luminance (or alpha with mask-type) scales coverage. This file
// resolves clip-path=/mask= references and tests points against the clip union
// using the existing boolean kernel.
#include <optional>
#include <string>
#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector {
struct SvgDocument;
struct SvgElement;

// One clip shape as flattened rings (in the clipPath's user space).
struct ClipShape {
    std::vector<std::vector<std::pair<double, double>>> rings;
    bool evenOdd = false;
};

// Resolve clip-path="url(#id)" for `el` into clip shapes (empty when none).
// `docToClip` maps document space into the clipPath user space when the
// caller needs it; rings are returned in document space already.
std::vector<ClipShape> clipFor(const SvgElement& el, const SvgDocument& doc);

// Point-in-clip test (union of shapes, honoring each shape's fill rule).
bool pointInClip(const std::vector<ClipShape>& clip, double x, double y);

// Intersect a subject polyline set with the clip union (for the exporter and
// the rasterizer fast path). Returns the surviving fragments.
std::vector<std::vector<std::pair<double, double>>> applyClipToPolylines(
    const std::vector<std::vector<std::pair<double, double>>>& subject,
    const std::vector<ClipShape>& clip);

// Mask: luminance-to-coverage lookup built from a <mask> subtree's flattened
// alpha. The rasterizer samples maskValueAt for each covered pixel.
struct MaskField {
    int w = 0, h = 0;
    std::vector<std::uint8_t> alpha;  // w*h coverage 0..255
    double x = 0.0, y = 0.0;          // placement in document space
    double opacity = 1.0;
    bool empty() const { return w <= 0 || h <= 0 || alpha.empty(); }
    float valueAt(double px, double py) const;  // bilinear, 0..1
};

// Build a mask field placeholder from the <mask> id (geometry resolved by the
// caller-supplied raster callback in UI code; engine stays toolkit-free, so
// this returns the mask element + bbox for the caller to rasterize).
struct MaskRef {
    std::string id;
    const SvgElement* element = nullptr;
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};
std::optional<MaskRef> maskFor(const SvgElement& el, const SvgDocument& doc);

}  // namespace pittore::vector
