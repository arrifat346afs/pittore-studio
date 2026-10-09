// Stripe math. Last stripe clamps to height.
#include "engine/vector/svg/png_stripes.h"

namespace pittore::svg {

std::vector<Stripe> splitStripes(int height, int stripeH) {
    std::vector<Stripe> out;
    if (height <= 0 || stripeH <= 0) {
        return out;
    }
    for (int y = 0; y < height; y += stripeH) {
        int h = height - y;
        if (h > stripeH) {
            h = stripeH;
        }
        out.push_back(Stripe{y, h});
    }
    return out;
}

}  // namespace pittore::svg
