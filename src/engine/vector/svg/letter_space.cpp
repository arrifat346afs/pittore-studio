// Normal is 0.
#include "engine/vector/svg/letter_space.h"

#include "engine/vector/svg/length.h"

namespace pittore::svg {

double spacingPx(const std::string& s, double fontPx) {
    if (s.empty() || s == "normal") {
        return 0;
    }
    const Length l = parseLength(s);
    return l.valid ? toPx(l, fontPx, fontPx, fontPx / 2.0) : 0;
}

}  // namespace pittore::svg
