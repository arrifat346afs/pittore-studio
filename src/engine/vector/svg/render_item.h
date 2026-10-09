#pragma once
// Flat paint list from scene. Skips hidden nodes.
#include <memory>
#include <string>
#include <vector>

#include "engine/vector/svg/path_data.h"
#include "engine/vector/svg/style.h"
#include "engine/vector/svg/transform.h"

namespace pittore::svg {

struct Scene;
struct SceneNode;

enum class ItemKind {
    Group,
    Rect,
    Circle,
    Ellipse,
    Line,
    Poly,
    Path,
    Use,
    Image,
    Text
};

struct RenderItem {
    ItemKind kind = ItemKind::Group;
    std::string id;
    Affine world;
    Style style;
    double x = 0, y = 0, w = 0, h = 0;
    double rx = 0, ry = 0;
    double cx = 0, cy = 0, r = 0;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    std::vector<std::pair<double, double>> points;
    std::vector<PathSeg> segs;
    std::string href;
    std::string text;
    bool closePoly = false;
};

// Depth-first paint order. Groups kept for isolation.
void appendRenderItems(const SceneNode& n, std::vector<RenderItem>& out);
std::vector<RenderItem> flattenScene(const Scene& scene);

}  // namespace pittore::svg
