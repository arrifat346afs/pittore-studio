// Length plus adjust word.
#include "engine/vector/svg/text_length.h"

#include <cstdlib>

namespace pittore::svg {

TextLength parseTextLength(const std::string& len, const std::string& adjust) {
    TextLength o;
    if (len.empty()) {
        return o;
    }
    char* end = nullptr;
    const double v = std::strtod(len.c_str(), &end);
    if (end == len.c_str()) {
        return o;
    }
    o.len = v;
    o.has = true;
    o.spacingAndGlyphs = adjust == "spacingAndGlyphs";
    return o;
}

}  // namespace pittore::svg
