#pragma once
// Live text flow: text-on-path, text-in-shape, linked frames.
//
// The text engine shapes runs; this file lays glyph advances along a
// flattened spine (on-path) or inside a bbox/polygon column set
// (in-shape/flow). Glyph rasterization stays with the text engine; here each
// glyph gets a position+rotation to stamp.
#include <string>
#include <vector>
#include <array>

namespace pittore::vector {

struct FlowGlyph {
    char32_t ch = 0;
    double x = 0.0, y = 0.0;  // baseline origin in document space
    double rotationDeg = 0.0;
    double advance = 0.0;
    bool visible = true;
};

// Lay `text` along the flattened `spine` (arclength walk). `glyphAdvance`
// maps each char to its advance (from the shaper); letterSpacing adds tracking.
std::vector<FlowGlyph> textOnPathLayout(const std::string& text,
                                        const std::vector<std::pair<double, double>>& spine,
                                        const std::vector<double>& glyphAdvance,
                                        double letterSpacing = 0.0,
                                        double startOffset = 0.0);

// Flow `text` inside `columns` (bboxes in reading order) with `lineHeight`
// and per-glyph advances. Returns one FlowGlyph per char (rotation 0);
// words wrap on spaces when `wrap` is set.
std::vector<FlowGlyph> textInShapeLayout(const std::string& text,
                                          const std::vector<std::array<double, 4>>& columns,
                                          const std::vector<double>& glyphAdvance,
                                          double lineHeight, bool wrap = true);

}  // namespace pittore::vector
