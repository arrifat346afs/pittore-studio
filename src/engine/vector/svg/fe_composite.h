#pragma once
// Porter-Duff plus arithmetic.
#include "engine/vector/svg/fe_buffer.h"

namespace pittore::svg {

enum class CompOp { Over, In, Out, Atop, Xor, Arithmetic };

FeImg feComposite(CompOp op, const FeImg& fg, const FeImg& bg, double k1 = 0,
                  double k2 = 0, double k3 = 0, double k4 = 0);

}  // namespace pittore::svg
