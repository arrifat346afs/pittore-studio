#pragma once
// Thread count from items and grain.
#include <cstdint>

namespace pittore::svg {

unsigned pickThreads(std::uint32_t items, std::uint32_t grain,
                     unsigned hw);

}  // namespace pittore::svg
