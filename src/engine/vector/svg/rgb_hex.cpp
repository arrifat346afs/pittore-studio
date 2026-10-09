// Clamp 0..1 then hex.
#include "engine/vector/svg/rgb_hex.h"

#include <cstdio>

namespace pittore::svg {

std::string rgbHex(float r, float g, float b) {
    auto q = [](float v) {
        int k = (int)(v * 255 + 0.5f);
        return k < 0 ? 0 : k > 255 ? 255 : k;
    };
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", q(r), q(g), q(b));
    return buf;
}

}  // namespace pittore::svg
