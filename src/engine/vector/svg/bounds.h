#pragma once
// Item bounds in world space. Stroke pads the box.
namespace pittore::svg {

struct RenderItem;

struct BBox {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = true;
};

BBox itemBounds(const RenderItem& it);
BBox unionBox(const BBox& a, const BBox& b);

}  // namespace pittore::svg
