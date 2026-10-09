#pragma once
// Scene to buffer at scale. Empty when over cap.
#include "engine/vector/svg/fe_buffer.h"

namespace pittore::svg {

struct Scene;

FeImg renderScene(const Scene& scene, double scale);

}  // namespace pittore::svg
