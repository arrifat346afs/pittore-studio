#pragma once
// Gzip magic check.
#include <cstdint>
#include <cstddef>

namespace pittore::svg {

bool hasGzipMagic(const std::uint8_t* data, std::size_t n);

}  // namespace pittore::svg
