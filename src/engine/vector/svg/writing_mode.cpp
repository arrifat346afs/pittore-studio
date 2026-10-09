// Vertical modes prefix.
#include "engine/vector/svg/writing_mode.h"

namespace pittore::svg {

bool isVerticalText(const std::string& mode) {
    return mode.compare(0, 2, "tb") == 0 || mode.rfind("vertical", 0) == 0;
}

}  // namespace pittore::svg
