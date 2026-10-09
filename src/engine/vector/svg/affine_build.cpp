// Single-op matrices.
#include "engine/vector/svg/affine_build.h"

#include <cmath>

namespace pittore::svg {

Affine makeTranslate(double tx, double ty) {
    Affine m;
    m.e = tx;
    m.f = ty;
    return m;
}

Affine makeScale(double sx, double sy) {
    Affine m;
    m.a = sx;
    m.d = sy;
    return m;
}

Affine makeRotate(double deg) {
    const double r = deg * 3.141592653589793 / 180.0;
    Affine m;
    m.a = std::cos(r);
    m.b = std::sin(r);
    m.c = -m.b;
    m.d = m.a;
    return m;
}

}  // namespace pittore::svg
