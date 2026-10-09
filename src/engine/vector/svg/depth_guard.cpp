// Strict greater only.
#include "engine/vector/svg/depth_guard.h"

namespace pittore::svg {

bool overDepth(int depth, int cap) {
    return depth > cap;
}

}  // namespace pittore::svg
