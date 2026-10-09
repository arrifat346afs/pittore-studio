#pragma once
// UTF-16 BOM detect. 0 none, 1 BE, 2 LE.
#include <cstdint>
#include <cstddef>

namespace pittore::svg {

int utf16Kind(const std::uint8_t* data, std::size_t n);

}  // namespace pittore::svg
