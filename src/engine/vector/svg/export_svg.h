#pragma once
// Scene to SVG text. Stable order, short floats.
#include <string>

namespace pittore::svg {

struct Scene;

std::string emitSceneSvg(const Scene& scene);

}  // namespace pittore::svg
