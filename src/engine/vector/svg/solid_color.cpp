// Reuse color parse.
#include "engine/vector/svg/solid_color.h"

#include "engine/vector/svg/color_parse.h"
#include "engine/vector/svg/stop_opacity.h"

namespace pittore::svg {

bool solidRgba(const std::string& color, const std::string& opacity,
               float out[4]) {
    if (!parseColor(color, out)) {
        return false;
    }
    out[3] *= stopAlpha(opacity, 1.0f);
    return true;
}

}  // namespace pittore::svg
