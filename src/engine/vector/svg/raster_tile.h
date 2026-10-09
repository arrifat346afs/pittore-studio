#pragma once
// Fill one item into a float buffer. Normal blend only.
#include "engine/vector/svg/fe_buffer.h"

namespace pittore::svg {

struct RenderItem;

void paintItemFlat(const RenderItem& it, FeImg& buf);

}  // namespace pittore::svg
