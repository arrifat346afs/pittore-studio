#pragma once
// Separable box blur. Radius in px.
#include "engine/vector/svg/fe_buffer.h"

namespace pittore::svg {

FeImg feBlurBox(const FeImg& src, int radius);

}  // namespace pittore::svg
