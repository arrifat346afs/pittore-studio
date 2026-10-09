// Swap keyword for color value.
#include "engine/vector/svg/current_color.h"

namespace pittore::svg {

bool resolveCurrentColor(const std::string& paint, const std::string& color,
                         std::string& out) {
    if (paint != "currentColor" && paint != "currentcolor") {
        out = paint;
        return false;
    }
    out = color.empty() ? "black" : color;
    return true;
}

}  // namespace pittore::svg
