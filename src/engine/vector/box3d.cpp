#include "engine/vector/box3d.h"

namespace pittore::vector {

std::array<std::vector<std::pair<double, double>>, 6> boxFaces(const Box3D& box) {
    auto P = [&](double a, double b, double c) -> std::pair<double, double> {
        return {box.ox + a * box.ex + b * box.fx + c * box.gx,
                box.oy + a * box.ey + b * box.fy + c * box.gy};
    };
    auto O = P(0, 0, 0), X = P(1, 0, 0), Y = P(0, 1, 0), Z = P(0, 0, 1);
    auto XY = P(1, 1, 0), XZ = P(1, 0, 1), YZ = P(0, 1, 1), XYZ = P(1, 1, 1);
    return {std::vector<std::pair<double, double>>{O, X, XY, Y},
            std::vector<std::pair<double, double>>{Z, XZ, XYZ, YZ},
            std::vector<std::pair<double, double>>{O, Y, YZ, Z},
            std::vector<std::pair<double, double>>{X, XY, XYZ, XZ},
            std::vector<std::pair<double, double>>{O, X, XZ, Z},
            std::vector<std::pair<double, double>>{Y, XY, XYZ, YZ}};
}

std::vector<Segment> boxToSegments(const Box3D& box) {
    std::vector<Segment> out;
    auto faces = boxFaces(box);
    // Painter order: depth faces first when depth points up-screen.
    std::array<int, 3> vis = {1, 4, 0};
    if (box.gy > 0) vis = {0, 5, 1};
    for (int f : vis) {
        auto& q = faces[(size_t)f];
        out.push_back(Segment{Segment::Kind::MoveTo, (float)q[0].first, (float)q[0].second});
        for (size_t i = 1; i < q.size(); i++)
            out.push_back(
                Segment{Segment::Kind::LineTo, (float)q[i].first, (float)q[i].second});
        out.push_back(Segment{Segment::Kind::Close});
    }
    return out;
}

}  // namespace pittore::vector
