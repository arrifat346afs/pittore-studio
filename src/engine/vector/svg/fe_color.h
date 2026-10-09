#pragma once
// Color matrix and alpha ops.
#include <vector>

#include "engine/vector/svg/fe_buffer.h"

namespace pittore::svg {

FeImg feApplyMatrix(const FeImg& src, const std::vector<double>& m20);
FeImg feLuminanceToAlpha(const FeImg& src);

}  // namespace pittore::svg
