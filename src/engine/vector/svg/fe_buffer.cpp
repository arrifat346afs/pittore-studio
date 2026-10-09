// Blank buffer maker.
#include "engine/vector/svg/fe_buffer.h"

namespace pittore::svg {

FeImg makeFeImg(int w, int h, float r, float g, float b, float a) {
    FeImg o;
    o.w = w;
    o.h = h;
    o.px.assign((size_t)(w > 0 && h > 0 ? w * h * 4 : 0), 0);
    for (size_t k = 0; k < o.px.size(); k += 4) {
        o.px[k] = r;
        o.px[k + 1] = g;
        o.px[k + 2] = b;
        o.px[k + 3] = a;
    }
    return o;
}

}  // namespace pittore::svg
