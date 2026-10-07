#pragma once
// Pattern and hatch fills: tiled bitmap/vector cells.
//
// A pattern is a <pattern> tile (width/height + optional patternTransform +
// child art) referenced by fill="url(#id)". A hatch is a <hatch> with
// pitch/rotation/stroke used by the "Hatches (rough)" LPE family. This file
// models the tile parameters and evaluates the tile transform; raster code
// stamps the cell.
#include <optional>
#include <string>
#include <vector>

namespace pittore::vector {
struct SvgDocument;
struct SvgElement;

// One pattern tile's geometry in user space.
struct PatternTile {
    std::string id;
    double x = 0.0, y = 0.0;      // pattern x/y offset
    double width = 10.0, height = 10.0;
    double transform[6] = {1, 0, 0, 1, 0, 0};  // patternTransform
    bool hasContent = false;
    // Which cell (col,row) covers point (x,y)? Handles negative coords.
    void cellAt(double x, double y, long& col, long& row) const;
    // Origin of cell (col,row) in user space (pre-transform).
    void cellOrigin(long col, long row, double& ox, double& oy) const;
};

std::optional<PatternTile> patternFor(const std::string& id,
                                      const SvgDocument& doc);

// Hatch: parallel strokes clipped to the shape.
struct HatchSpec {
    std::string id;
    double pitch = 6.0;       // spacing between strokes
    double rotationDeg = 0.0;  // hatch rotation
    double strokeWidth = 1.0;
    double strokeRgba[4] = {0, 0, 0, 1};
};
std::optional<HatchSpec> hatchFor(const std::string& id, const SvgDocument& doc);

// Built-in library (>20 geometric + hatch stock presets). Returns <defs> SVG
// text for injection.
std::string builtinPatternsSvg();
std::vector<std::string> builtinPatternIds();

// 30+ dash presets (stroke-dash stock sets + classic CAD sets).
// Each entry is {name, dash array in stroke-width units}.
std::vector<std::pair<std::string, std::vector<float>>> builtinDashPresets();

}  // namespace pittore::vector
