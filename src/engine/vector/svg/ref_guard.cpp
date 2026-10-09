// Strict greater only.
#include "engine/vector/svg/ref_guard.h"

namespace pittore::svg {

bool overRefCap(std::size_t n, std::size_t cap) {
    return n > cap;
}

}  // namespace pittore::svg
