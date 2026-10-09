// RTL words.
#include "engine/vector/svg/bidi_flag.h"

namespace pittore::svg {

bool isRtlDir(const std::string& dir) {
    return dir == "rtl";
}

}  // namespace pittore::svg
