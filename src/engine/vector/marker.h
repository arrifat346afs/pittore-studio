#pragma once
// Markers: start/mid/end ornaments (arrowheads, dots).
//
// A marker is a small SVG subtree in `defs` referenced by
// marker-start/mid/end="url(#id)". `orient="auto"` rotates the marker to the
// path tangent; `markerUnits="strokeWidth"` (default) scales by stroke width.
// This file models marker lookup + instantiation geometry; stroking code
// consumes MarkerInstance lists.
#include <optional>
#include <string>
#include <vector>

namespace pittore::vector {
struct SvgDocument;
struct SvgElement;

// Where a marker sits on a path.
struct MarkerVertex {
    double x = 0.0, y = 0.0;
    double tangentDeg = 0.0;  // direction of travel
    bool isStart = false, isEnd = false;
};

// One placed marker after resolving orient/scale.
struct MarkerInstance {
    std::string markerId;
    double x = 0.0, y = 0.0;
    double rotationDeg = 0.0;
    double scale = 1.0;  // includes strokeWidth when markerUnits=strokeWidth
    double opacity = 1.0;
};

// Collect marker instances for a flattened polyline. `markers` carries the
// three resolved marker ids (empty = none). `strokeWidth` feeds
// markerUnits="strokeWidth" scaling.
std::vector<MarkerInstance> markersForPolyline(
    const std::vector<std::pair<double, double>>& points, bool closed,
    const std::string& markerStart, const std::string& markerMid,
    const std::string& markerEnd, double strokeWidth);

// Look up marker-* properties for `el` (presentation attrs + style cascade).
struct MarkerRefs {
    std::string start, mid, end;
};
MarkerRefs markerRefsFor(const SvgElement& el, const SvgDocument& doc);

// Marker element metrics: refX/refY, orient, markerUnits, markerWidth/Height.
struct MarkerDef {
    std::string id;
    double refX = 0.0, refY = 0.0;
    double width = 3.0, height = 3.0;  // markerWidth/Height in marker units
    bool orientAuto = false;
    double orientDeg = 0.0;
    bool strokeWidthUnits = true;
};
std::optional<MarkerDef> markerDefFor(const std::string& id, const SvgDocument& doc);

// Built-in arrow/dot library (30 entries: Arrow1/2, Triangle, Dot, Square,
// Diamond, Cross, ...). Returns SVG text for <defs> injection; ids are
// `mk-<name>`.
std::string builtinMarkersSvg();
std::vector<std::string> builtinMarkerIds();

}  // namespace pittore::vector
