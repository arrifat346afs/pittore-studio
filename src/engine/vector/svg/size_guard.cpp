// Same caps as import path.
#include "engine/vector/svg/size_guard.h"

namespace pittore::svg {

bool overSizeCap(std::size_t bytes) {
    return bytes > 64u * 1024u * 1024u;
}

bool overTagCap(int tags) {
    return tags > 512 * 1024;
}

bool overUseCap(int uses) {
    return uses > 64 * 1024;
}

}  // namespace pittore::svg
