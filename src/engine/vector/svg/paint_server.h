#pragma once
// Paint to flat color via gradient store.
#include <map>
#include <string>

#include "engine/vector/svg/color_parse.h"
#include "engine/vector/svg/gradient.h"

namespace pittore::svg {

struct GradientStore {
    std::map<std::string, Gradient> byId;
};

bool resolvePaint(const Paint& p, const GradientStore& gs, double t,
                  float out[4]);

}  // namespace pittore::svg
