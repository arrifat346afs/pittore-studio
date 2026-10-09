#pragma once
// Glyph orientation. Auto flag plus angle.
#include <string>

namespace pittore::svg {

struct GlyphOrient {
    bool autoMode = true;
    double deg = 0;
};

GlyphOrient parseGlyphOrient(const std::string& s);

}  // namespace pittore::svg
