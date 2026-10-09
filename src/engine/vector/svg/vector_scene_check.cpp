// Cross-check on rect/circle fixtures. View plus count.
#include "engine/vector/svg/vector_scene_check.h"

#include <cmath>

#include "engine/vector/svg/import_bridge.h"
#include "engine/vector/vector_scene.h"

namespace pittore::svg {

bool checkFlatParity(const std::string& xml) {
    const pittore::vector::VectorScene flat =
        pittore::vector::parse_svg(xml, "check");
    const ImportedSvg tree = importSvg(xml);
    if (!tree.ok) {
        return false;
    }
    if (std::abs(flat.view_w - (float)tree.scene.viewW) > 0.5f) {
        return false;
    }
    if (std::abs(flat.view_h - (float)tree.scene.viewH) > 0.5f) {
        return false;
    }
    int shapes = 0;
    for (const auto& it : tree.items) {
        if (it.kind != ItemKind::Group) {
            ++shapes;
        }
    }
    return shapes == (int)flat.prims.size();
}

}  // namespace pittore::svg
