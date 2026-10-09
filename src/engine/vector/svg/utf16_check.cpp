// FE FF is BE, FF FE is LE.
#include "engine/vector/svg/utf16_check.h"

namespace pittore::svg {

int utf16Kind(const std::uint8_t* data, std::size_t n) {
    if (n >= 2 && data[0] == 0xfe && data[1] == 0xff) {
        return 1;
    }
    if (n >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        return 2;
    }
    return 0;
}

}  // namespace pittore::svg
