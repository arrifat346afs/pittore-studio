#pragma once
// Stripe coverage check.
#include <vector>

#include "engine/vector/svg/png_stripes.h"

namespace pittore::svg {

bool stripesCover(const std::vector<Stripe>& v, int height);

}  // namespace pittore::svg
