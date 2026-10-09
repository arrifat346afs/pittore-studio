// Small blend set. Straight alpha in, out.
#include "engine/vector/svg/blend.h"

#include <algorithm>

namespace pittore::svg {
namespace {

float chan(Blend m, float s, float d) {
    switch (m) {
        case Blend::Multiply:
            return s * d;
        case Blend::Screen:
            return s + d - s * d;
        case Blend::Darken:
            return std::min(s, d);
        case Blend::Lighten:
            return std::max(s, d);
        case Blend::Overlay:
            return d < 0.5f ? 2 * s * d : 1 - 2 * (1 - s) * (1 - d);
        case Blend::Difference:
            return s > d ? s - d : d - s;
        default:
            return s;
    }
}

}  // namespace

void blendPx(Blend m, const float src[4], const float dst[4], float out[4]) {
    if (m == Blend::Normal) {
        out[0] = src[0];
        out[1] = src[1];
        out[2] = src[2];
        out[3] = src[3];
        return;
    }
    out[3] = src[3] + dst[3] * (1 - src[3]);
    for (int k = 0; k < 3; ++k) {
        out[k] = chan(m, src[k], dst[k]);
    }
}

}  // namespace pittore::svg
