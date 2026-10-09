#pragma once
// Ref map cap check.
#include <cstddef>

namespace pittore::svg {

bool overRefCap(std::size_t n, std::size_t cap = 65536);

}  // namespace pittore::svg
