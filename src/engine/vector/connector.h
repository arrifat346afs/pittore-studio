#pragma once
// Diagram connectors: routed polylines with obstacle avoidance.
//
// Full orthogonal maze-routing is out of scope for the engine; this implements
// straight/polyline/orthogonal routing on a coarse grid with bbox-obstacle
// penalties (A*), which covers flowcharts and UML without a vendored solver.
#include <vector>

namespace pittore::vector {

enum class ConnectorKind { Straight, Polyline, Orthogonal };

struct ConnectorObstacle {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct ConnectorRoute {
    std::vector<std::pair<double, double>> points;
    bool orthogonal = false;
};

// Route from `a` to `b` avoiding `obstacles` (document space).
ConnectorRoute routeConnector(std::pair<double, double> a, std::pair<double, double> b,
                              ConnectorKind kind,
                              const std::vector<ConnectorObstacle>& obstacles);

}  // namespace pittore::vector
