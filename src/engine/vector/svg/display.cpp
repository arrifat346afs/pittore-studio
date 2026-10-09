// Exact words only.
#include "engine/vector/svg/display.h"

namespace pittore::svg {

bool isDisplayNone(const std::string& display) {
    return display == "none";
}

bool isVisible(const std::string& visibility) {
    return visibility.empty() || visibility == "visible";
}

}  // namespace pittore::svg
