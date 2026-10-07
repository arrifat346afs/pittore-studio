#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/render/layer_style.h"

namespace pittore::render {
namespace detail {

void boxRadii(float sigma, int out[3]);
void boxPass(const float* src, float* dst, int w, int h, int r, bool vertical);
void gaussianBlurPlane(std::vector<float>& a, int w, int h, float radius);

}  // namespace detail
}  // namespace pittore::render
