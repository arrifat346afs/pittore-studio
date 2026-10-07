#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/render/layer_style.h"

namespace pittore::render {
namespace detail {

float sepBlend(StyleBlend m, float b, float s);
StyleColor blendPixel(StyleBlend mode, const StyleColor& top, const StyleColor& bottom);

}  // namespace detail
}  // namespace pittore::render
