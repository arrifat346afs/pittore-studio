#pragma once
// Float RGBA buffer for filter units.
#include <vector>

namespace pittore::svg {

struct FeImg {
    int w = 0, h = 0;
    std::vector<float> px;
};

FeImg makeFeImg(int w, int h, float r = 0, float g = 0, float b = 0,
                float a = 0);

}  // namespace pittore::svg
