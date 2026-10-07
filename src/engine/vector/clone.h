#pragma once
// <use>, <symbol> and <defs> clones: live linked copies.
//
// A clone keeps a target id plus the transform on the <use>; expansion copies
// the referenced subtree with cloned transforms concatenated. Tiled clones
// stamp a grid/radial lattice of copies with per-tile shift/scale/rotation
// (the subset the editor exposes; unknown tiled attributes round-trip as
// strings).
#include <optional>
#include <string>
#include <vector>

#include "engine/vector/svg_dom.h"

namespace pittore::vector {

// Deep-copy `el` (ids suffixed so the document stays unique).
std::shared_ptr<SvgElement> deepCopyElement(const SvgElement& el,
                                            const std::string& idSuffix);

// Expand one <use href="#id" x/y/width/height transform>: returns the
// referenced subtree with the use's translation+transform prepended. Nullopt
// when the target is missing (broken link, kept as-is on save).
std::optional<std::shared_ptr<SvgElement>> expandUse(const SvgElement& use,
                                                     const SvgDocument& doc,
                                                     const std::string& idSuffix = "-u");

// Recursively expand every <use> in the tree (cycle-guarded, depth <= 16).
// Returns the number of expansions performed.
int expandAllUses(SvgDocument& doc);

// Unlink: replace <use> nodes with their expansion in place (deep=true also
// unlinks nested uses).
int unlinkClones(SvgDocument& doc, bool deep);

// Tiled-clone lattice kinds.
enum class TileKind { Grid, Radial, Spiral };
// Parameters for one tiling operation.
struct TileSpec {
    TileKind kind = TileKind::Grid;
    int cols = 3, rows = 3;      // grid
    double dx = 10.0, dy = 10.0;  // tile step
    double scaleStep = 0.0;       // +fraction per column
    double rotateStep = 0.0;      // degrees per tile
    int radialCount = 8;          // radial/spiral copies
    double radialR = 50.0;        // radial radius
};
// Stamp copies of `source` (already in document space) into `parent`.
// Returns the created nodes. Pure geometry: sets transform="translate.."
// (+rotate/scale) on each clone wrapper <g>.
std::vector<std::shared_ptr<SvgElement>> tileClones(const SvgElement& source,
                                                    const TileSpec& spec);

// Relief: list every <use> with its target id and whether it resolves.
struct UseLink {
    SvgElement* node = nullptr;
    std::string target;
    bool resolves = false;
};
std::vector<UseLink> listUseLinks(SvgDocument& doc);

}  // namespace pittore::vector
