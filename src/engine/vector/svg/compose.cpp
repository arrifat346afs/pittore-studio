// Paint in order. Groups carry no pixels.
#include "engine/vector/svg/compose.h"

#include "engine/vector/svg/raster_tile.h"
#include "engine/vector/svg/render_item.h"

namespace pittore::svg {

FeImg composeItems(const std::vector<RenderItem>& items, int w, int h) {
    FeImg buf = makeFeImg(w, h, 0, 0, 0, 0);
    for (const auto& it : items) {
        if (it.kind == ItemKind::Group) {
            continue;
        }
        paintItemFlat(it, buf);
    }
    return buf;
}

}  // namespace pittore::svg
