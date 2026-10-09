// Small-size composite. Mismatched sizes return fg.
#include "engine/vector/svg/fe_composite.h"

namespace pittore::svg {

FeImg feComposite(CompOp op, const FeImg& fg, const FeImg& bg, double k1,
                  double k2, double k3, double k4) {
    if (fg.w != bg.w || fg.h != bg.h) {
        return fg;
    }
    FeImg out = fg;
    for (size_t k = 0; k < fg.px.size(); k += 4) {
        const float fa = fg.px[k + 3], ba = bg.px[k + 3];
        float a = fa, r = fg.px[k], g = fg.px[k + 1], b = fg.px[k + 2];
        if (op == CompOp::Over) {
            a = fa + ba * (1 - fa);
        } else if (op == CompOp::In) {
            a = fa * ba;
            r *= ba;
            g *= ba;
            b *= ba;
        } else if (op == CompOp::Out) {
            a = fa * (1 - ba);
            r *= 1 - ba;
            g *= 1 - ba;
            b *= 1 - ba;
        } else if (op == CompOp::Atop) {
            a = ba;
            r = fg.px[k] * ba + bg.px[k] * (1 - fa);
            g = fg.px[k + 1] * ba + bg.px[k + 1] * (1 - fa);
            b = fg.px[k + 2] * ba + bg.px[k + 2] * (1 - fa);
        } else if (op == CompOp::Xor) {
            a = fa + ba - 2 * fa * ba;
        } else {
            for (int c = 0; c < 3; ++c) {
                out.px[k + c] =
                    (float)(k1 * fg.px[k + c] * bg.px[k + c] + k2 * fg.px[k + c] +
                            k3 * bg.px[k + c] + k4);
            }
            out.px[k + 3] = a;
            continue;
        }
        out.px[k] = r;
        out.px[k + 1] = g;
        out.px[k + 2] = b;
        out.px[k + 3] = a;
    }
    return out;
}

}  // namespace pittore::svg
