#pragma once
// Text length adjust mode.
#include <string>

namespace pittore::svg {

struct TextLength {
    double len = 0;
    bool has = false;
    bool spacingAndGlyphs = false;
};

TextLength parseTextLength(const std::string& len, const std::string& adjust);

}  // namespace pittore::svg
