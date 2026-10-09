// Viewport math. Numbers only, no units here.
#include "engine/vector/svg/viewbox.h"

#include <cstdlib>

#include "engine/vector/svg/length.h"

namespace pittore::svg {

bool parseViewBox(const std::string& s, double& w, double& h) {
    const char* p = s.c_str();
    char* end = nullptr;
    double v[4] = {0, 0, 0, 0};
    for (int k = 0; k < 4; ++k) {
        v[k] = std::strtod(p, &end);
        if (end == p) {
            return false;
        }
        p = end;
    }
    w = v[2];
    h = v[3];
    return w > 0 && h > 0;
}

Viewport resolveViewport(const std::string& vb, const std::string& wStr,
                         const std::string& hStr) {
    Viewport out;
    double vw = 0, vh = 0;
    if (!vb.empty() && parseViewBox(vb, vw, vh)) {
        out.w = vw;
        out.h = vh;
        return out;
    }
    const Length w = parseLength(wStr);
    const Length h = parseLength(hStr);
    out.w = w.valid ? toPx(w, 1000.0) : 0;
    out.h = h.valid ? toPx(h, 1000.0) : 0;
    if (out.w <= 0) {
        out.w = 1000;
    }
    if (out.h <= 0) {
        out.h = 1000;
    }
    return out;
}

}  // namespace pittore::svg
