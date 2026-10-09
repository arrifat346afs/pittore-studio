// Scale view to buffer. Caps at 2048 px a side.
#include "engine/vector/svg/scene_image.h"

#include <cmath>

#include "engine/vector/svg/compose.h"
#include "engine/vector/svg/render_item.h"
#include "engine/vector/svg/scene.h"
#include "engine/vector/svg/transform.h"

namespace pittore::svg {

FeImg renderScene(const Scene& scene, double scale) {
    FeImg empty;
    if (!scene.ok || !scene.root || scale <= 0) {
        return empty;
    }
    const int w = (int)std::round(scene.viewW * scale);
    const int h = (int)std::round(scene.viewH * scale);
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) {
        return empty;
    }
    auto items = flattenScene(scene);
    // Apply output scale on top of world matrices.
    const Affine s = Affine{scale, 0, 0, scale, 0, 0};
    for (auto& it : items) {
        it.world = multiply(s, it.world);
    }
    return composeItems(items, w, h);
}

}  // namespace pittore::svg
