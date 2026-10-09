// Adjugate over determinant.
#include "engine/vector/svg/affine_inv.h"

namespace pittore::svg {

bool invertAffine(const Affine& m, Affine& out) {
    const double det = m.a * m.d - m.b * m.c;
    if (det == 0) {
        return false;
    }
    out.a = m.d / det;
    out.b = -m.b / det;
    out.c = -m.c / det;
    out.d = m.a / det;
    out.e = (m.c * m.f - m.d * m.e) / det;
    out.f = (m.b * m.e - m.a * m.f) / det;
    return true;
}

}  // namespace pittore::svg
