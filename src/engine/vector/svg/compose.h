#pragma once
// Stack items onto a buffer. Source-over order.
#include <vector>

#include "engine/vector/svg/fe_buffer.h"

namespace pittore::svg {

struct RenderItem;

FeImg composeItems(const std::vector<RenderItem>& items, int w, int h);

}  // namespace pittore::svg
