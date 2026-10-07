// Connector routing (coarse A* for orthogonal, direct otherwise).
#include "engine/vector/connector.h"

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <vector>
#include <unordered_map>

namespace pittore::vector {
namespace {

// Grid A* over an inflated obstacle field. Cells are `cell` doc units;
// obstacles inflate by one cell so routes keep clearance.
struct Grid {
    double ox = 0, oy = 0, cell = 8.0;
    int w = 0, h = 0;
    std::vector<std::uint8_t> blocked;
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    bool free(int x, int y) const { return inside(x, y) && !blocked[(size_t)(y * w + x)]; }
};

std::vector<std::pair<double, double>> astarRoute(
    std::pair<double, double> a, std::pair<double, double> b,
    const std::vector<ConnectorObstacle>& obstacles) {
    const double margin = 32.0, cell = 8.0;
    double x0 = std::min(a.first, b.first) - margin;
    double y0 = std::min(a.second, b.second) - margin;
    double x1 = std::max(a.first, b.first) + margin;
    double y1 = std::max(a.second, b.second) + margin;
    Grid g;
    g.ox = x0;
    g.oy = y0;
    g.cell = cell;
    g.w = (int)std::ceil((x1 - x0) / cell) + 1;
    g.h = (int)std::ceil((y1 - y0) / cell) + 1;
    if (g.w * g.h > 40000 || g.w <= 0 || g.h <= 0) return {};
    g.blocked.assign((size_t)g.w * g.h, 0);
    auto toCell = [&](double x, double y) {
        return std::pair<int, int>{(int)std::round((x - x0) / cell),
                                   (int)std::round((y - y0) / cell)};
    };
    for (auto& o : obstacles) {
        auto [cx0, cy0] = toCell(o.x0 - cell, o.y0 - cell);
        auto [cx1, cy1] = toCell(o.x1 + cell, o.y1 + cell);
        for (int y = cy0; y <= cy1; y++)
            for (int x = cx0; x <= cx1; x++)
                if (g.inside(x, y)) g.blocked[(size_t)(y * g.w + x)] = 1;
    }
    auto [sx, sy] = toCell(a.first, a.second);
    auto [tx, ty] = toCell(b.first, b.second);
    sx = std::min(g.w - 1, std::max(0, sx));
    sy = std::min(g.h - 1, std::max(0, sy));
    tx = std::min(g.w - 1, std::max(0, tx));
    ty = std::min(g.h - 1, std::max(0, ty));
    // Endpoints always routable (unblock their cells: connectors dock inside).
    g.blocked[(size_t)(sy * g.w + sx)] = 0;
    g.blocked[(size_t)(ty * g.w + tx)] = 0;
    auto key = [&](int x, int y) { return y * g.w + x; };
    auto heur = [&](int x, int y) { return std::abs(x - tx) + std::abs(y - ty); };
    using Node = std::tuple<int, int, int>;  // (f, x, y)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    std::unordered_map<int, int> gScore, came;
    open.emplace(heur(sx, sy), sx, sy);
    gScore[key(sx, sy)] = 0;
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    bool found = (sx == tx && sy == ty);
    int expansions = 0;
    while (!open.empty() && !found && expansions++ < 40000) {
        auto [f, x, y] = open.top();
        open.pop();
        if (x == tx && y == ty) {
            found = true;
            break;
        }
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (!g.free(nx, ny)) continue;
            int ng = gScore[key(x, y)] + 1;
            auto it = gScore.find(key(nx, ny));
            if (it == gScore.end() || ng < it->second) {
                gScore[key(nx, ny)] = ng;
                came[key(nx, ny)] = key(x, y);
                open.emplace(ng + heur(nx, ny), nx, ny);
            }
        }
    }
    if (!found) return {};
    std::vector<std::pair<int, int>> cells;
    int ck = key(tx, ty);
    cells.emplace_back(tx, ty);
    while (ck != key(sx, sy)) {
        auto it = came.find(ck);
        if (it == came.end()) return {};
        ck = it->second;
        cells.emplace_back(ck % g.w, ck / g.w);
    }
    std::reverse(cells.begin(), cells.end());
    // Cell centers → doc points, then drop collinear middles.
    std::vector<std::pair<double, double>> pts{{a.first, a.second}};
    for (size_t i = 1; i + 1 < cells.size(); i++) {
        auto [px, py] = cells[i - 1];
        auto [cx, cy] = cells[i];
        auto [nx, ny] = cells[i + 1];
        if ((cx - px) * (ny - cy) == (cy - py) * (nx - cx)) continue;  // straight
        pts.emplace_back(x0 + cx * cell, y0 + cy * cell);
    }
    pts.emplace_back(b.first, b.second);
    return pts;
}

}  // namespace

ConnectorRoute routeConnector(std::pair<double, double> a, std::pair<double, double> b,
                              ConnectorKind kind,
                              const std::vector<ConnectorObstacle>& obstacles) {
    if (kind == ConnectorKind::Straight) return ConnectorRoute{{a, b}, false};
    if (kind == ConnectorKind::Polyline) {
        auto mid = std::pair<double, double>{(a.first + b.first) / 2, (a.second + b.second) / 2};
        return ConnectorRoute{{a, mid, b}, false};
    }
    auto pts = astarRoute(a, b, obstacles);
    if (pts.size() >= 2) return ConnectorRoute{pts, true};
    // Fallback: L/Z branch with fewer obstacle crossings.
    std::pair<double, double> elbow{b.first, a.second};
    std::pair<double, double> elbow2{a.first, b.second};
    auto crossings = [&](const std::vector<std::pair<double, double>>& q) {
        int hits = 0;
        for (size_t i = 0; i + 1 < q.size(); i++)
            for (auto& o : obstacles) {
                double x0 = std::min(q[i].first, q[i + 1].first);
                double x1 = std::max(q[i].first, q[i + 1].first);
                double y0 = std::min(q[i].second, q[i + 1].second);
                double y1 = std::max(q[i].second, q[i + 1].second);
                if (x1 >= o.x0 && x0 <= o.x1 && y1 >= o.y0 && y0 <= o.y1) hits++;
            }
        return hits;
    };
    std::vector<std::pair<double, double>> r1{a, elbow, b};
    std::vector<std::pair<double, double>> r2{a, elbow2, b};
    if (crossings(r2) < crossings(r1)) return ConnectorRoute{r2, true};
    return ConnectorRoute{r1, true};
}

}  // namespace pittore::vector
