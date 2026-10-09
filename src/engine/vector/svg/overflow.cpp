// Hidden and scroll both clip.
#include "engine/vector/svg/overflow.h"

namespace pittore::svg {

bool clipsOverflow(const std::string& overflow) {
    return overflow == "hidden" || overflow == "scroll";
}

}  // namespace pittore::svg
