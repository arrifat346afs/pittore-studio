// Sum heights and check order.
#include "engine/vector/svg/stripe_join.h"

namespace pittore::svg {

bool stripesCover(const std::vector<Stripe>& v, int height) {
    int y = 0;
    for (const auto& s : v) {
        if (s.y0 != y || s.h <= 0) {
            return false;
        }
        y += s.h;
    }
    return y == height;
}

}  // namespace pittore::svg
