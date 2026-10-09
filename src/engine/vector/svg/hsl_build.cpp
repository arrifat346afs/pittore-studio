// Standard hsl math.
#include "engine/vector/svg/hsl_build.h"

namespace pittore::svg {
namespace {

float chan(float p, float q, float t) {
    if (t < 0) {
        t += 1;
    }
    if (t > 1) {
        t -= 1;
    }
    if (t < 1.0f / 6) {
        return p + (q - p) * 6 * t;
    }
    if (t < 0.5f) {
        return q;
    }
    if (t < 2.0f / 3) {
        return p + (q - p) * (2.0f / 3 - t) * 6;
    }
    return p;
}

}  // namespace

void hslToRgb(float h, float s, float l, float out[3]) {
    float hh = h - (int)(h / 360) * 360;
    if (hh < 0) {
        hh += 360;
    }
    hh /= 360;
    if (s == 0) {
        out[0] = out[1] = out[2] = l;
        return;
    }
    const float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    const float p = 2 * l - q;
    out[0] = chan(p, q, hh + 1.0f / 3);
    out[1] = chan(p, q, hh);
    out[2] = chan(p, q, hh - 1.0f / 3);
}

}  // namespace pittore::svg
