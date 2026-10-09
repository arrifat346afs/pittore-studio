// 1f 8b magic.
#include "engine/vector/svg/zip_magic.h"

namespace pittore::svg {

bool hasGzipMagic(const std::uint8_t* data, std::size_t n) {
    return n >= 2 && data[0] == 0x1f && data[1] == 0x8b;
}

}  // namespace pittore::svg
