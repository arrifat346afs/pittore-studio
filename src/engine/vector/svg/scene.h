#pragma once
// Typed scene tree built from XML. Keeps refs for later passes.
#include <memory>
#include <string>
#include <vector>

#include "engine/vector/svg/path_data.h"
#include "engine/vector/svg/style.h"
#include "engine/vector/svg/transform.h"

namespace pittore::svg {

enum class NodeKind {
    Group,
    Rect,
    Circle,
    Ellipse,
    Line,
    Polyline,
    Polygon,
    Path,
    Use,
    Image,
    Text
};

struct SceneNode {
    NodeKind kind = NodeKind::Group;
    std::string id;
    Affine local;
    Affine world;
    Style style;
    // Geometry in local space.
    double x = 0, y = 0, w = 0, h = 0;
    double rx = 0, ry = 0;
    double cx = 0, cy = 0, r = 0;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    std::vector<std::pair<double, double>> points;
    std::vector<PathSeg> segs;
    // Use and image refs.
    std::string href;
    std::string text;
    std::vector<std::shared_ptr<SceneNode>> children;
};

struct Scene {
    std::shared_ptr<SceneNode> root;
    double viewW = 0, viewH = 0;
    bool ok = false;
};

// Build from parsed XML plus sheet rules.
Scene buildScene(const XmlNode& root, const std::vector<CssRule>& sheet);

}  // namespace pittore::svg
