#pragma once
// Stripe split for banded export. Pure math.
#include <vector>

namespace pittore::svg {

struct Stripe {
    int y0 = 0, h = 0;
};

std::vector<Stripe> splitStripes(int height, int stripeH = 64);

}  // namespace pittore::svg
