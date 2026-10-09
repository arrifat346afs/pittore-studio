// Medium is 16. Small/large scale it.
#include "engine/vector/svg/font_size.h"

#include "engine/vector/svg/length.h"

namespace pittore::svg {

double fontSizePx(const std::string& s, double parentPx, double fb) {
    if (s.empty()) {
        return parentPx > 0 ? parentPx : fb;
    }
    if (s == "medium") {
        return 16.0;
    }
    if (s == "small") {
        return 13.0;
    }
    if (s == "large") {
        return 19.0;
    }
    if (s == "larger") {
        return (parentPx > 0 ? parentPx : fb) * 1.2;
    }
    if (s == "smaller") {
        return (parentPx > 0 ? parentPx : fb) / 1.2;
    }
    const Length l = parseLength(s);
    if (!l.valid) {
        return parentPx > 0 ? parentPx : fb;
    }
    return toPx(l, parentPx > 0 ? parentPx : fb, parentPx > 0 ? parentPx : 16.0,
                (parentPx > 0 ? parentPx : 16.0) / 2.0);
}

}  // namespace pittore::svg
