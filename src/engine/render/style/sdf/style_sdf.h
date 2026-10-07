#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/render/layer_style.h"

namespace pittore::render {
namespace detail {

void dt1d(const std::vector<float>& f, std::vector<float>& d, std::vector<int>& v,
            std::vector<float>& z);
std::vector<float> edt(const std::vector<std::uint8_t>& feature, int w, int h);
std::vector<float> signedDistance(const std::vector<float>& alpha, int w, int h, float limit);

}  // namespace detail
}  // namespace pittore::render
