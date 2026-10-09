// Fixed name set.
#include "engine/vector/svg/filter_in.h"

namespace pittore::svg {

bool isMagicInput(const std::string& in) {
    return in == "SourceGraphic" || in == "SourceAlpha" ||
           in == "BackgroundImage" || in == "BackgroundAlpha" ||
           in == "FillPaint" || in == "StrokePaint";
}

}  // namespace pittore::svg
