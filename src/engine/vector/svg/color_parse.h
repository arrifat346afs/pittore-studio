#pragma once
// Paint string to color or ref. Linear output.
#include <string>
#include <string_view>

namespace pittore::svg {

// One fill or stroke value.
struct Paint {
    bool none = true;
    float r = 0, g = 0, b = 0, a = 1;
    bool hasRef = false;
    std::string ref;
};

// Full paint parse: none, color, url(#id).
Paint parsePaint(std::string_view s);

// Plain color to linear rgba. False when not a color.
bool parseColor(std::string_view s, float rgba[4]);

// Id from url(#id). Empty when not a ref.
std::string paintRef(std::string_view s);

}  // namespace pittore::svg
