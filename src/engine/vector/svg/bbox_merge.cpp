// Fold unions.
#include "engine/vector/svg/bbox_merge.h"

namespace pittore::svg {

BBox mergeBoxes(const std::vector<BBox>& boxes) {
    BBox out;
    for (const auto& b : boxes) {
        out = unionBox(out, b);
    }
    return out;
}

}  // namespace pittore::svg
