// Auto or degrees.
#include "engine/vector/svg/glyph_orient.h"

#include "engine/vector/svg/angle.h"

namespace pittore::svg {

GlyphOrient parseGlyphOrient(const std::string& s) {
    GlyphOrient o;
    if (s.empty() || s == "auto") {
        return o;
    }
    o.autoMode = false;
    o.deg = parseAngleDeg(s);
    return o;
}

}  // namespace pittore::svg
