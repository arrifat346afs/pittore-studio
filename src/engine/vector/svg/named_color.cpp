// Small table. sRGB inputs, linear out.
#include "engine/vector/svg/named_color.h"

#include "engine/vector/svg/color_parse.h"

namespace pittore::svg {

bool namedRgba(const std::string& name, float out[4]) {
    if (name == "yellow" || name == "cyan" || name == "magenta" ||
        name == "orange" || name == "purple" || name == "pink" ||
        name == "lime" || name == "navy" || name == "teal" ||
        name == "olive" || name == "maroon" || name == "silver" ||
        name == "aqua" || name == "fuchsia" || name == "brown") {
        return parseColor(name == "yellow"   ? "rgb(255,255,0)"
                          : name == "cyan"   ? "rgb(0,255,255)"
                          : name == "magenta" ? "rgb(255,0,255)"
                          : name == "orange" ? "rgb(255,165,0)"
                          : name == "purple" ? "rgb(128,0,128)"
                          : name == "pink"   ? "rgb(255,192,203)"
                          : name == "lime"   ? "rgb(0,255,0)"
                          : name == "navy"   ? "rgb(0,0,128)"
                          : name == "teal"   ? "rgb(0,128,128)"
                          : name == "olive"  ? "rgb(128,128,0)"
                          : name == "maroon" ? "rgb(128,0,0)"
                          : name == "silver" ? "rgb(192,192,192)"
                          : name == "aqua"   ? "rgb(0,255,255)"
                          : name == "fuchsia" ? "rgb(255,0,255)"
                                              : "rgb(165,42,42)",
                          out);
    }
    if (name == "transparent") {
        out[0] = out[1] = out[2] = 0;
        out[3] = 0;
        return true;
    }
    return parseColor(name, out);
}

}  // namespace pittore::svg
