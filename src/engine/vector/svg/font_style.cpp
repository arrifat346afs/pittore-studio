// Italic words.
#include "engine/vector/svg/font_style.h"

namespace pittore::svg {

bool isItalicStyle(const std::string& s) {
    return s == "italic" || s == "oblique";
}

}  // namespace pittore::svg
