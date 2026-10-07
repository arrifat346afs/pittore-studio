// Vector drag gestures: creation on empty canvas, modify-in-place
// when existing art is hit. All commits go through addVectorPathLayer /
// applyVectorNode (one undo step each), reusing the engine builders in
// src/engine/vector (calligraphy, spray, box3d, connector, lpe, path_ops).
// Live previews paint from paintInkPreview (guarded overlay hook); all
// gesture state below is TU-local — no shared canvas state is touched.
#include "ui/canvas_view.h"

#include <cmath>
#include <cstdio>

#include "engine/vector/box3d.h"
#include "engine/vector/boolean.h"
#include "engine/vector/calligraphy.h"
#include "engine/vector/connector.h"
#include "engine/vector/lpe/lpe.h"
#include "engine/vector/path_ops.h"
#include "engine/vector/spiro.h"
#include "engine/vector/spray.h"
#include "engine/vector/transform_ops.h"
#include "ui/tool_registry.h"

namespace pittore::ui {
namespace {

// Connector endpoint re-drag across press→release (single view; reset every
// press, consumed on release).
int g_connLayer = -1;
int g_connEnd = 0;  // 0 = move end (keep start), 1 = move start (keep end)
bool g_connRedrag = false;
QPointF g_connFixed;

// LPE option-combo order (MUST match options_draw.cpp "lpe_effect").
const vector::lpe::EffectType kLpeChoices[] = {
    vector::lpe::EffectType::Offset,         vector::lpe::EffectType::FilletChamfer,
    vector::lpe::EffectType::Simplify,       vector::lpe::EffectType::BendPath,
    vector::lpe::EffectType::Envelope,       vector::lpe::EffectType::Roughen,
    vector::lpe::EffectType::Sketch,         vector::lpe::EffectType::TaperStroke,
    vector::lpe::EffectType::Powerstroke,    vector::lpe::EffectType::CopyRotate,
    vector::lpe::EffectType::MirrorSymmetry, vector::lpe::EffectType::Tiling,
    vector::lpe::EffectType::Gears,          vector::lpe::EffectType::VonKoch,
    vector::lpe::EffectType::Extrude,        vector::lpe::EffectType::MeasureSegments,
    vector::lpe::EffectType::Powerclip,
};
constexpr int kLpeChoiceCount = 17;
static_assert(sizeof(kLpeChoices) / sizeof(kLpeChoices[0]) == kLpeChoiceCount,
              "lpe_effect combo order drift");

bool isInkTool(ToolId t) {
    return t == ToolId::CalligraphyTool || t == ToolId::SprayTool ||
           t == ToolId::MeshTool || t == ToolId::ConnectorTool ||
           t == ToolId::TweakTool || t == ToolId::Box3DTool ||
           t == ToolId::PagesTool || t == ToolId::LpeTool ||
           t == ToolId::MarkerTool || t == ToolId::VectorEraserTool;
}

std::vector<vector::Segment> discSegments(double cx, double cy, double r, int n = 16) {
    std::vector<vector::Segment> out{
        vector::Segment{vector::Segment::Kind::MoveTo, (float)(cx + r), (float)cy}};
    for (int i = 1; i <= n; i++) {
        double a = 2 * 3.14159265358979 * i / n;
        out.push_back(vector::Segment{vector::Segment::Kind::LineTo,
                                      (float)(cx + r * std::cos(a)),
                                      (float)(cy + r * std::sin(a))});
    }
    out.push_back(vector::Segment{vector::Segment::Kind::Close});
    return out;
}

std::vector<vector::Segment> rectSegments(double x0, double y0, double x1, double y1) {
    return {vector::Segment{vector::Segment::Kind::MoveTo, (float)x0, (float)y0},
            vector::Segment{vector::Segment::Kind::LineTo, (float)x1, (float)y0},
            vector::Segment{vector::Segment::Kind::LineTo, (float)x1, (float)y1},
            vector::Segment{vector::Segment::Kind::LineTo, (float)x0, (float)y1},
            vector::Segment{vector::Segment::Kind::Close}};
}

// Shared gesture math (preview + release compute identical results) --------

double tweakGestureRadius(double x0, double y0, double x1, double y1) {
    return std::max(8.0, std::hypot(x1 - x0, y1 - y0) * 2 + 8.0);
}

double gestureDrag(double x0, double y0, double x1, double y1) {
    return std::hypot(x1 - x0, y1 - y0);
}

// Layer document transform (mirrors the overlay composition exactly so
// endpoints, previews and commits agree on coordinates).
QTransform layerDocTransform(const LayerItem& l) {
    const vector::ArtNode* art = l.art.get();
    const double m[6] = {art ? art->matrix[0] : 1.0, art ? art->matrix[1] : 0.0,
                         art ? art->matrix[2] : 0.0, art ? art->matrix[3] : 1.0,
                         art ? art->matrix[4] : 0.0, art ? art->matrix[5] : 0.0};
    return QTransform(m[0], m[1], m[2], m[3], m[4], m[5]) *
           QTransform().scale(l.scaleX, l.scaleY) *
           QTransform().translate(l.offset.x(), l.offset.y());
}

// Obstacle rects from vector art bounds (document space) for A* avoidance.
std::vector<vector::ConnectorObstacle> artObstacles(const DocumentItem& d,
                                                    int skipLayer = -1) {
    std::vector<vector::ConnectorObstacle> out;
    for (int i = 0; i < (int)d.layers.size() && (int)out.size() < 64; i++) {
        if (i == skipLayer) continue;
        const LayerItem& l = d.layers[i];
        if (!l.visible || !l.art || l.art->isEmpty()) continue;
        vector::Path flat = vector::flattenSegments(l.art->segments, 1.0f);
        double x0 = 1e100, y0 = 1e100, x1 = -1e100, y1 = -1e100;
        for (auto& sp : flat.subpaths)
            for (auto [px, py] : sp) {
                x0 = std::min(x0, (double)px);
                y0 = std::min(y0, (double)py);
                x1 = std::max(x1, (double)px);
                y1 = std::max(y1, (double)py);
            }
        if (!(x1 > x0 && y1 > y0)) continue;
        const QTransform t = layerDocTransform(l);
        const QRectF r = t.mapRect(QRectF(x0, y0, x1 - x0, y1 - y0));
        out.push_back({r.x(), r.y(), r.x() + r.width(), r.y() + r.height()});
    }
    return out;
}

// Spray stamps shared by preview and commit (identical inputs → identical
// stamps): cone from the bar's radius/scatter/scale, density from the drag.
// Clone mode lays a uniform tile grid (live-linked look); otherwise scatter.
std::vector<vector::SprayStamp> sprayGestureStamps(double x0, double y0,
                                                   double x1, double y1,
                                                   double radiusOpt,
                                                   double scatterPct,
                                                   double scalePct,
                                                   bool clone = false) {
    vector::SpraySpec spec;
    spec.radius = std::max(8.0, std::max(radiusOpt, gestureDrag(x0, y0, x1, y1) / 2));
    spec.scatter = clone ? 0.0 : qBound(0.0, scatterPct, 100.0) / 100.0;
    const double base = qBound(0.1, scalePct, 400.0) / 100.0;
    spec.scaleMin = spec.scaleMax = clone ? base : base;
    if (!clone) {
        spec.scaleMin = base * 0.5;
        spec.scaleMax = base * 1.5;
    }
    spec.rotationJitter = clone ? 0.0 : 180.0;
    const int n = (int)std::min(64.0, std::max(4.0, gestureDrag(x0, y0, x1, y1) / 4));
    if (clone) spec.seed = 1;  // stable lattice preview == commit
    auto stamps = vector::sprayStamps((x0 + x1) / 2, (y0 + y1) / 2, n, spec);
    if (clone && !stamps.empty()) {
        // Uniform tile lattice instead of a scatter pile.
        const int cols = (int)std::ceil(std::sqrt(stamps.size()));
        const double step = spec.radius * 2 / std::max(1, cols);
        for (size_t i = 0; i < stamps.size(); i++) {
            stamps[i].x = (x0 + x1) / 2 - spec.radius + (i % cols + 0.5) * step;
            stamps[i].y = (y0 + y1) / 2 - spec.radius + (i / cols + 0.5) * step;
            stamps[i].rotationDeg = 0.0;
            stamps[i].opacity = 1.0;
        }
    }
    return stamps;
}

vector::lpe::EffectType lpeEffectForIndex(int i) {
    if (i >= 0 && i < kLpeChoiceCount) return kLpeChoices[i];
    return vector::lpe::EffectType::Offset;
}

}  // namespace

bool CanvasView::inkPress(const QPointF& docPoint) {
    const ToolId tool = state_->activeTool();
    if (!isInkTool(tool)) return false;
    inkActive_ = true;
    inkTool_ = tool;
    inkStartDoc_ = inkCurDoc_ = docPoint;
    inkSpine_.clear();
    inkSpine_.emplace_back(docPoint.x(), docPoint.y());
    g_connRedrag = false;
    g_connLayer = -1;
    // Connector re-drag: press near an endpoint of a Connector layer picks it
    // up (release re-routes keeping the other end fixed).
    if (tool == ToolId::ConnectorTool) {
        DocumentItem* d = state_->activeDocument();
        if (d) {
            const double tol = 8.0 / qMax(1e-9, d->zoom);
            for (int i = 0; i < (int)d->layers.size(); i++) {
                const LayerItem& l = d->layers[i];
                if (!l.art || l.art->isEmpty()) continue;
                if (!l.name.startsWith(toolName(ToolId::ConnectorTool),
                                       Qt::CaseInsensitive))
                    continue;
                vector::Path flat = vector::flattenSegments(l.art->segments, 0.5f);
                if (flat.subpaths.empty() || flat.subpaths[0].size() < 2) continue;
                const QTransform t = layerDocTransform(l);
                const auto& sp = flat.subpaths[0];
                const QPointF a = t.map(QPointF(sp.front().first, sp.front().second));
                const QPointF b = t.map(QPointF(sp.back().first, sp.back().second));
                if (QLineF(docPoint, a).length() <= tol) {
                    g_connRedrag = true;
                    g_connLayer = i;
                    g_connEnd = 1;  // moving start, keep end
                    g_connFixed = b;
                    break;
                }
                if (QLineF(docPoint, b).length() <= tol) {
                    g_connRedrag = true;
                    g_connLayer = i;
                    g_connEnd = 0;  // moving end, keep start
                    g_connFixed = a;
                    break;
                }
            }
        }
    }
    return true;
}

bool CanvasView::inkMove(const QPointF& docPoint) {
    if (!inkActive_) return false;
    inkCurDoc_ = docPoint;
    inkSpine_.emplace_back(docPoint.x(), docPoint.y());
    viewport()->update();
    return true;
}

bool CanvasView::inkRelease(const QPointF& docPoint) {
    if (!inkActive_) return false;
    inkActive_ = false;
    inkCurDoc_ = docPoint;
    DocumentItem* d = state_->activeDocument();
    if (!d) return true;
    const ToolId tool = inkTool_;
    const double x0 = inkStartDoc_.x(), y0 = inkStartDoc_.y();
    const double x1 = docPoint.x(), y1 = docPoint.y();
    const auto undoName = toolName(tool);

    // Modify-in-place upgrades: tweak pushes hit art, eraser bites it, LPE
    // offsets it. Creation fallback below covers the empty-canvas audit.
    const int hit = topPixelLayerAt(*d, inkStartDoc_);
    const bool hasArt = hit >= 0 && hit < (int)d->layers.size() && d->layers[(size_t)hit].art &&
                        !d->layers[(size_t)hit].art->isEmpty();

    if (tool == ToolId::TweakTool && hasArt) {
        vector::ArtNode work = *d->layers[(size_t)hit].art;
        QPointF nodePos;
        if (docToArtNode(hit, inkCurDoc_, &nodePos)) {
            const double radius = tweakGestureRadius(x0, y0, x1, y1);
            const double amount =
                state_->option(tool, QStringLiteral("tweak_force")).toDouble() / 10.0;
            const int mode =
                state_->option(tool, QStringLiteral("tweak_mode")).toInt();
            const double amp = amount > 0 ? amount : 3.0;
            if (mode == 4) {
                // Color: hue-shift the paint (fill + gradient stops) by the
                // drag angle fraction — vector recolor without rasterizing.
                auto hueShift = [](std::uint8_t c[4], double turns) {
                    double r = c[0] / 255.0, g = c[1] / 255.0, b = c[2] / 255.0;
                    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
                    double h = 0, s = 0, v = mx;
                    if (mx > mn) {
                        double d = mx - mn;
                        s = d / mx;
                        if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
                        else if (mx == g) h = (b - r) / d + 2;
                        else h = (r - g) / d + 4;
                        h /= 6;
                    }
                    h += turns - std::floor(h + turns);
                    double hh = h * 6;
                    int i = (int)hh;
                    double f = hh - i, p = v * (1 - s), q = v * (1 - f * s),
                           t = v * (1 - (1 - f) * s);
                    double rr = v, gg = v, bb = v;
                    if (i == 0) { rr = v; gg = t; bb = p; }
                    else if (i == 1) { rr = q; gg = v; bb = p; }
                    else if (i == 2) { rr = p; gg = v; bb = t; }
                    else if (i == 3) { rr = p; gg = q; bb = v; }
                    else if (i == 4) { rr = t; gg = p; bb = v; }
                    else { rr = v; gg = p; bb = q; }
                    c[0] = (std::uint8_t)(rr * 255);
                    c[1] = (std::uint8_t)(gg * 255);
                    c[2] = (std::uint8_t)(bb * 255);
                };
                const double turns = (x1 - x0) / 200.0;
                hueShift(work.paint.fill, turns);
                for (auto& stop : work.paint.gradient.stops)
                    hueShift(stop.rgba, turns);
                state_->applyVectorNode(hit, work, undoName);
                refresh();
                return true;
            }
            // Geometry modes on the editable segments (node space).
            // 0 push, 1 shrink, 2 grow (scale about centroid), 3 roughen.
            double cx = 0, cy = 0, cn = 0;
            for (const auto& s : work.segments) {
                if (s.kind == vector::Segment::Kind::Close) continue;
                cx += s.x;
                cy += s.y;
                cn++;
            }
            if (cn > 0) {
                cx /= cn;
                cy /= cn;
            }
            std::uint32_t seed =
                (std::uint32_t)(nodePos.x() * 13 + nodePos.y() * 71 + 7);
            auto rnd = [&]() {
                seed = seed * 1664525u + 1013904223u;
                return (seed >> 8) / 16777216.0 - 0.5;
            };
            for (auto& s : work.segments) {
                if (s.kind == vector::Segment::Kind::Close) continue;
                double dx = s.x - nodePos.x(), dy = s.y - nodePos.y();
                double dist = std::hypot(dx, dy);
                if (dist >= radius || dist < 1e-9) continue;
                const double fall = 0.5 + 0.5 * std::cos(dist / radius * 3.14159265358979);
                if (mode == 3) {
                    s.x = (float)(s.x + rnd() * amp * fall * 2);
                    s.y = (float)(s.y + rnd() * amp * fall * 2);
                } else if (mode == 2) {
                    const double g = 1 + amp / 50 * fall;
                    s.x = (float)(cx + (s.x - cx) * g);
                    s.y = (float)(cy + (s.y - cy) * g);
                } else {
                    const double dir = (mode == 1) ? -1.0 : 1.0;
                    const double k = dir * amp * fall;
                    s.x = (float)(s.x + dx / dist * k);
                    s.y = (float)(s.y + dy / dist * k);
                }
            }
            state_->applyVectorNode(hit, work, undoName);
            refresh();
            return true;
        }
    }
    if (tool == ToolId::VectorEraserTool && hasArt) {
        // Delete mode removes the whole layer; Trim (default) bites a disc.
        if (state_->option(tool, QStringLiteral("veraser_mode")).toInt() == 1) {
            state_->removeLayerSilently(hit);
            refresh();
            return true;
        }
        vector::ArtNode work = *d->layers[(size_t)hit].art;
        QPointF nodePos;
        if (docToArtNode(hit, inkCurDoc_, &nodePos)) {
            double w = state_->option(tool, QStringLiteral("veraser_width")).toDouble();
            if (!(w > 0)) w = 12.0;
            vector::Path flat = vector::flattenSegments(work.segments, 0.5f);
            std::vector<vector::BoolRing> rings;
            for (auto& sp : flat.subpaths) {
                vector::BoolRing r;
                for (auto [px, py] : sp) r.emplace_back(px, py);
                if (r.size() >= 3) rings.push_back(r);
            }
            // Disc in node space (isotropic: average layer scale).
            const double sc = std::max(
                1e-9, (std::abs(d->layers[(size_t)hit].scaleX) +
                       std::abs(d->layers[(size_t)hit].scaleY)) /
                          2.0);
            const double nr = w / 2 / sc;
            vector::BoolRing disc;
            for (int i = 0; i < 16; i++) {
                double a = 2 * 3.14159265358979 * i / 16;
                disc.emplace_back(nodePos.x() + nr * std::cos(a),
                                  nodePos.y() + nr * std::sin(a));
            }
            std::vector<vector::BoolRing> acc;
            for (auto& ring : rings) {
                auto parts =
                    vector::booleanOp({ring}, {disc}, vector::BoolOp::Difference);
                acc.insert(acc.end(), parts.begin(), parts.end());
            }
            std::vector<vector::Segment> segs;
            for (auto& r : acc) {
                if (r.empty()) continue;
                segs.push_back(vector::Segment{vector::Segment::Kind::MoveTo,
                                               (float)r[0].first, (float)r[0].second});
                for (size_t i = 1; i < r.size(); i++)
                    segs.push_back(vector::Segment{vector::Segment::Kind::LineTo,
                                                   (float)r[i].first, (float)r[i].second});
                segs.push_back(vector::Segment{vector::Segment::Kind::Close});
            }
            if (!segs.empty()) {
                work.segments = segs;
                state_->applyVectorNode(hit, work, undoName);
            } else {
                state_->removeLayerSilently(hit);
            }
            refresh();
            return true;
        }
    }
    if (tool == ToolId::LpeTool && hasArt) {
        vector::ArtNode work = *d->layers[(size_t)hit].art;
        QPointF pressNode, curNode;
        if (docToArtNode(hit, inkStartDoc_, &pressNode) &&
            docToArtNode(hit, inkCurDoc_, &curNode)) {
            const auto fxType = lpeEffectForIndex(
                state_->option(tool, QStringLiteral("lpe_effect")).toInt());
            const double amount =
                state_->option(tool, QStringLiteral("lpe_amount")).toDouble();
            vector::lpe::Params p = vector::lpe::dragParamsFor(
                fxType, pressNode.x(), pressNode.y(), curNode.x(), curNode.y(),
                amount > 0 ? amount : 30.0);
            if (auto fx = vector::lpe::makeEffect(fxType, p)) {
                work.segments = fx->apply(work.segments);
                state_->applyVectorNode(hit, work, undoName);
                refresh();
                return true;
            }
        }
    }
    if (tool == ToolId::MarkerTool && hasArt) {
        // Live attach: stock ornaments on the chosen ends (creation below
        // bakes the ornament instead for empty canvas).
        vector::ArtNode work = *d->layers[(size_t)hit].art;
        const int pos = state_->option(tool, QStringLiteral("marker_pos")).toInt();
        if (pos == 0 || pos == 3) work.paint.markerStart = "mk-Arrow1";
        if (pos == 1 || pos == 3) work.paint.markerMid = "mk-Dot";
        if (pos == 2 || pos == 3) work.paint.markerEnd = "mk-Arrow1";
        state_->applyVectorNode(hit, work, undoName);
        refresh();
        return true;
    }
    if (tool == ToolId::MeshTool && hasArt &&
        d->layers[(size_t)hit].art->paint.hasMesh) {
        // Drag the nearest lattice corner by the press→release delta.
        vector::ArtNode work = *d->layers[(size_t)hit].art;
        QPointF pressNode, curNode;
        if (docToArtNode(hit, inkStartDoc_, &pressNode) &&
            docToArtNode(hit, inkCurDoc_, &curNode)) {
            double dx = curNode.x() - pressNode.x();
            double dy = curNode.y() - pressNode.y();
            double best = 1e100;
            size_t bi = 0, bj = 0;
            for (size_t i = 0; i < work.paint.mesh.patches.size(); i++)
                for (size_t j = 0; j < 16; j++) {
                    double ddx = work.paint.mesh.patches[i].p[j][0] - pressNode.x();
                    double ddy = work.paint.mesh.patches[i].p[j][1] - pressNode.y();
                    double dd = ddx * ddx + ddy * ddy;
                    if (dd < best) {
                        best = dd;
                        bi = i;
                        bj = j;
                    }
                }
            const double tol = 24.0 / qMax(1e-9, d->zoom);
            if (best <= tol * tol) {
                work.paint.mesh.patches[bi].p[bj][0] += dx;
                work.paint.mesh.patches[bi].p[bj][1] += dy;
                state_->applyVectorNode(hit, work, undoName);
                state_->setStatusHint(tr("Mesh: corner moved."));
            } else {
                state_->setStatusHint(tr("Mesh: drag from a lattice corner."));
            }
            refresh();
            return true;
        }
    }
    if (tool == ToolId::ConnectorTool && g_connRedrag && g_connLayer >= 0 &&
        g_connLayer < (int)d->layers.size()) {
        // Endpoint re-drag: re-route keeping the fixed end, in node space.
        const LayerItem& l = d->layers[g_connLayer];
        if (l.art && !l.art->isEmpty()) {
            const int kindIdx =
                state_->option(tool, QStringLiteral("conn_kind")).toInt();
            const bool avoid =
                state_->option(tool, QStringLiteral("conn_avoid")).toBool();
            const vector::ConnectorKind kind =
                kindIdx == 0 ? vector::ConnectorKind::Straight
                : kindIdx == 1 ? vector::ConnectorKind::Polyline
                               : vector::ConnectorKind::Orthogonal;
            const auto obs = avoid ? artObstacles(*d, g_connLayer)
                                   : std::vector<vector::ConnectorObstacle>{};
            const auto route = vector::routeConnector(
                {g_connFixed.x(), g_connFixed.y()}, {docPoint.x(), docPoint.y()},
                kind, obs);
            if (route.points.size() >= 2) {
                const QTransform t = layerDocTransform(l);
                bool invertible = false;
                const QTransform inv = t.inverted(&invertible);
                if (invertible) {
                    vector::ArtNode work = *l.art;
                    std::vector<vector::Segment> segs;
                    bool first = true;
                    for (auto [px, py] : route.points) {
                        const QPointF np = inv.map(QPointF(px, py));
                        segs.push_back(vector::Segment{
                            first ? vector::Segment::Kind::MoveTo
                                  : vector::Segment::Kind::LineTo,
                            (float)np.x(), (float)np.y()});
                        first = false;
                    }
                    work.segments = segs;
                    state_->applyVectorNode(g_connLayer, work, undoName);
                    state_->setStatusHint(tr("Connector: re-routed."));
                }
            }
            g_connRedrag = false;
            g_connLayer = -1;
            refresh();
            return true;
        }
        g_connRedrag = false;
        g_connLayer = -1;
    }

    // Creation gestures (empty canvas + audit path).
    std::vector<vector::Segment> segs;
    double drag = std::hypot(x1 - x0, y1 - y0);
    if (tool == ToolId::MeshTool && drag >= 2) {
        // Real 1x1 mesh over the drag rect (corner colors from foreground).
        double rx0 = std::min(x0, x1), ry0 = std::min(y0, y1);
        double rx1 = std::max(x0, x1), ry1 = std::max(y0, y1);
        segs = rectSegments(rx0, ry0, rx1, ry1);
        QColor fg = state_->foreground();
        auto corner = [&](double fx, double fy, int skew) {
            std::array<std::uint8_t, 4> c{(std::uint8_t)qBound(0, fg.red() - skew, 255),
                                          (std::uint8_t)qBound(0, fg.green() - skew / 2, 255),
                                          (std::uint8_t)qBound(0, fg.blue() + skew / 3, 255),
                                          255};
            (void)fx;
            (void)fy;
            return c;
        };
        vector::MeshGradient mesh;
        mesh.rows = mesh.cols = 1;
        if (state_->option(tool, QStringLiteral("mesh_conical")).toBool()) {
            mesh.isConical = true;
            mesh.conicalCx = mesh.conicalCy = 0.5;
        }
        vector::MeshPatch patch{};
        // Lattice in node (layer-local) space, matching the segments.
        const double w = rx1 - rx0, h = ry1 - ry0;
        patch.p = {{{0, 0},
                    {w / 3, 0},
                    {2 * w / 3, 0},
                    {w, 0},
                    {0, h / 3},
                    {w / 3, h / 3},
                    {2 * w / 3, h / 3},
                    {w, h / 3},
                    {0, 2 * h / 3},
                    {w / 3, 2 * h / 3},
                    {2 * w / 3, 2 * h / 3},
                    {w, 2 * h / 3},
                    {0, h},
                    {w / 3, h},
                    {2 * w / 3, h},
                    {w, h}}};
        patch.c = {corner(0, 0, 0), corner(1, 0, 24), corner(1, 1, 48), corner(0, 1, 72)};
        mesh.patches.push_back(patch);
        auto node = std::make_shared<vector::ArtNode>();
        node->name = toolName(tool).toStdString();
        node->segments = segs;
        node->paint.hasFill = true;
        node->paint.fill[0] = (std::uint8_t)fg.red();
        node->paint.fill[1] = (std::uint8_t)fg.green();
        node->paint.fill[2] = (std::uint8_t)fg.blue();
        node->paint.fill[3] = 255;
        node->paint.hasMesh = true;
        node->paint.mesh = mesh;
        QRectF frame(QPointF(rx0, ry0), QPointF(rx1, ry1));
        // Shift segments into layer-local space (commit tail expects it).
        for (auto& s : node->segments) {
            if (s.kind == vector::Segment::Kind::Close) continue;
            s.x = (float)(s.x - rx0);
            s.y = (float)(s.y - ry0);
            if (s.kind == vector::Segment::Kind::CubicTo) {
                s.c1x = (float)(s.c1x - rx0);
                s.c1y = (float)(s.c1y - ry0);
                s.c2x = (float)(s.c2x - rx0);
                s.c2y = (float)(s.c2y - ry0);
            }
        }
        // Mesh lattice rides in document space; keep node at frame origin.
        state_->commitArtNodeLayer(std::move(node), frame, tool, undoName, true);
        refresh();
        return true;
    }
    if (drag < 2) {
        // Bare click still leaves its dot (matches Vector Brush).
        segs = discSegments(x0, y0, 4.0);
    } else if (tool == ToolId::CalligraphyTool) {
        std::vector<vector::CalligraphySample> spine;
        for (auto [px, py] : inkSpine_) spine.push_back({px, py, 1.0, 0, 0});
        if (spine.size() < 2) spine.push_back({x1, y1, 1.0, 0, 0});
        vector::CalligraphyNib nib;
        nib.width = state_->option(tool, QStringLiteral("nib_width")).toDouble();
        if (!(nib.width > 0)) nib.width = 12.0;
        nib.angleDeg = state_->option(tool, QStringLiteral("nib_angle")).toDouble();
        if (nib.angleDeg == 0) nib.angleDeg = 30.0;
        nib.flatness =
            state_->option(tool, QStringLiteral("nib_flat")).toDouble() / 100.0;
        if (!(nib.flatness > 0)) nib.flatness = 0.15;
        nib.thinning =
            state_->option(tool, QStringLiteral("nib_thin")).toDouble() / 100.0;
        nib.mass = state_->option(tool, QStringLiteral("nib_mass")).toDouble() / 100.0;
        segs = vector::calligraphyStroke(spine, nib);
    } else if (tool == ToolId::SprayTool) {
        const double radiusOpt =
            state_->option(tool, QStringLiteral("spray_radius")).toDouble();
        const double scatterOpt =
            state_->option(tool, QStringLiteral("spray_scatter")).toDouble();
        const double scaleOpt =
            state_->option(tool, QStringLiteral("spray_scale")).toDouble();
        auto stamps = sprayGestureStamps(x0, y0, x1, y1,
                                         radiusOpt > 0 ? radiusOpt : 40.0,
                                         scatterOpt > 0 ? scatterOpt : 100.0,
                                         scaleOpt > 0 ? scaleOpt : 100.0);
        for (auto& st : stamps) {
            auto disc = discSegments(st.x, st.y, 3.0 * st.scale);
            segs.insert(segs.end(), disc.begin(), disc.end());
        }
    } else if (tool == ToolId::Box3DTool) {
        vector::Box3D box;
        box.ox = x0;
        box.oy = y0;
        box.ex = x1 - x0;
        box.fx = 0;
        box.fy = y1 - y0;
        const double depth =
            state_->option(tool, QStringLiteral("box_depth")).toDouble();
        const bool twoPt =
            state_->option(tool, QStringLiteral("box_2pt")).toBool();
        const double dz = depth != 0 ? depth : -40.0;
        box.gx = dz * 0.7;
        box.gy = twoPt ? dz * 0.7 : 0.0;
        box.twoPoint = twoPt;
        segs = vector::boxToSegments(box);
    } else if (tool == ToolId::ConnectorTool) {
        const int kindIdx =
            state_->option(tool, QStringLiteral("conn_kind")).toInt();
        const bool avoid =
            state_->option(tool, QStringLiteral("conn_avoid")).toBool();
        const vector::ConnectorKind kind =
            kindIdx == 0 ? vector::ConnectorKind::Straight
            : kindIdx == 1 ? vector::ConnectorKind::Polyline
                           : vector::ConnectorKind::Orthogonal;
        const auto obs = avoid ? artObstacles(*d) : std::vector<vector::ConnectorObstacle>{};
        auto route = vector::routeConnector({x0, y0}, {x1, y1}, kind, obs);
        segs.push_back(vector::Segment{vector::Segment::Kind::MoveTo,
                                       (float)route.points[0].first,
                                       (float)route.points[0].second});
        for (size_t i = 1; i < route.points.size(); i++)
            segs.push_back(vector::Segment{vector::Segment::Kind::LineTo,
                                           (float)route.points[i].first,
                                           (float)route.points[i].second});
    } else if (tool == ToolId::TweakTool) {
        auto circ = discSegments((x0 + x1) / 2, (y0 + y1) / 2, std::max(8.0, drag / 2));
        vector::Path flat = vector::flattenSegments(circ, 0.5f);
        std::vector<std::pair<double, double>> pts;
        for (auto& sp : flat.subpaths)
            for (auto [px, py] : sp) pts.emplace_back(px, py);
        auto disp = vector::tweakDisplace(pts, x0, y0, std::max(8.0, drag), 6.0);
        segs.push_back(vector::Segment{vector::Segment::Kind::MoveTo, (float)pts[0].first,
                                       (float)pts[0].second});
        for (size_t i = 1; i < pts.size(); i++)
            segs.push_back(vector::Segment{
                vector::Segment::Kind::LineTo, (float)(pts[i].first + disp[i].first),
                (float)(pts[i].second + disp[i].second)});
        segs.push_back(vector::Segment{vector::Segment::Kind::Close});
    } else if (tool == ToolId::VectorEraserTool) {
        // Eraser substrate: rect with a drag-disc bite (shows the boolean).
        auto rect = rectSegments(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1),
                                 std::max(y0, y1));
        vector::Path flat = vector::flattenSegments(rect, 0.5f);
        std::vector<vector::BoolRing> rings;
        for (auto& sp : flat.subpaths) {
            vector::BoolRing r;
            for (auto [px, py] : sp) r.emplace_back(px, py);
            if (r.size() >= 3) rings.push_back(r);
        }
        vector::BoolRing disc;
        double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, nr = std::max(4.0, drag / 4);
        for (int i = 0; i < 16; i++) {
            double a = 2 * 3.14159265358979 * i / 16;
            disc.emplace_back(cx + nr * std::cos(a), cy + nr * std::sin(a));
        }
        std::vector<vector::BoolRing> acc;
        for (auto& ring : rings) {
            auto parts = vector::booleanOp({ring}, {disc}, vector::BoolOp::Difference);
            acc.insert(acc.end(), parts.begin(), parts.end());
        }
        for (auto& r : acc) {
            if (r.empty()) continue;
            segs.push_back(vector::Segment{vector::Segment::Kind::MoveTo, (float)r[0].first,
                                           (float)r[0].second});
            for (size_t i = 1; i < r.size(); i++)
                segs.push_back(vector::Segment{vector::Segment::Kind::LineTo,
                                               (float)r[i].first, (float)r[i].second});
            segs.push_back(vector::Segment{vector::Segment::Kind::Close});
        }
        if (segs.empty()) segs = rect;
    } else if (tool == ToolId::LpeTool) {
        // Generative: chosen effect over a base disc at the press point.
        const auto fxType = lpeEffectForIndex(
            state_->option(tool, QStringLiteral("lpe_effect")).toInt());
        const double amount =
            state_->option(tool, QStringLiteral("lpe_amount")).toDouble();
        const double r = std::max(10.0, drag / 2);
        segs = discSegments(x0, y0, r);
        vector::lpe::Params p = vector::lpe::dragParamsFor(
            fxType, x0 - r, y0 - r, x1, y1, amount > 0 ? amount : 30.0);
        if (auto fx = vector::lpe::makeEffect(fxType, p)) {
            auto applied = fx->apply(segs);
            if (!applied.empty()) segs = applied;
        }
    } else if (tool == ToolId::MarkerTool) {
        // Arrowhead oriented along the drag, sized by the bar's scale.
        double mscale = state_->option(tool, QStringLiteral("marker_scale")).toDouble();
        if (!(mscale > 0)) mscale = 100.0;
        double a = std::atan2(y1 - y0, x1 - x0);
        double L = std::max(12.0, drag) * mscale / 100.0;
        double cx = x1, cy = y1;
        segs = {vector::Segment{vector::Segment::Kind::MoveTo, (float)cx, (float)cy},
                vector::Segment{vector::Segment::Kind::LineTo,
                                (float)(cx - L * std::cos(a - 0.4)),
                                (float)(cy - L * std::sin(a - 0.4))},
                vector::Segment{vector::Segment::Kind::LineTo,
                                (float)(cx - L * std::cos(a + 0.4)),
                                (float)(cy - L * std::sin(a + 0.4))},
                vector::Segment{vector::Segment::Kind::Close}};
    } else {
        // Pages frame rect (page order/export build on the frame).
        segs = rectSegments(std::min(x0, x1), std::min(y0, y1), std::max(x0, x1),
                            std::max(y0, y1));
    }
    if (!segs.empty() && d) state_->addVectorPathLayer(segs, tool, undoName);
    refresh();
    return true;
}

void CanvasView::paintInkPreview(QPainter& painter, const QTransform& viewMap) {
    if (!inkActive_) return;
    const ToolId tool = inkTool_;
    if (!isInkTool(tool)) return;
    const double x0 = inkStartDoc_.x(), y0 = inkStartDoc_.y();
    const double x1 = inkCurDoc_.x(), y1 = inkCurDoc_.y();
    const QColor blue(0x3d, 0xa5, 0xff);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(blue, 1.5));
    auto dot = [&](double x, double y, double r) {
        const QPointF v = viewMap.map(QPointF(x, y));
        // Radius in device px: scale the doc radius by the view scale.
        const double s = std::max(std::abs(viewMap.m11()), std::abs(viewMap.m22()));
        painter.drawEllipse(v, r * s, r * s);
    };
    if (tool == ToolId::SprayTool) {
        const double radiusOpt =
            state_->option(tool, QStringLiteral("spray_radius")).toDouble();
        const double scatterOpt =
            state_->option(tool, QStringLiteral("spray_scatter")).toDouble();
        const double scaleOpt =
            state_->option(tool, QStringLiteral("spray_scale")).toDouble();
        auto stamps = sprayGestureStamps(x0, y0, x1, y1,
                                         radiusOpt > 0 ? radiusOpt : 40.0,
                                         scatterOpt > 0 ? scatterOpt : 100.0,
                                         scaleOpt > 0 ? scaleOpt : 100.0);
        const QPointF c = viewMap.map(QPointF((x0 + x1) / 2, (y0 + y1) / 2));
        const double s = std::max(std::abs(viewMap.m11()), std::abs(viewMap.m22()));
        double r = 0;
        for (auto& st : stamps)
            r = std::max(r, std::hypot(st.x - (x0 + x1) / 2, st.y - (y0 + y1) / 2) +
                               3.0 * st.scale);
        painter.drawEllipse(c, r * s, r * s);
        painter.setBrush(blue);
        for (auto& st : stamps) dot(st.x, st.y, 1.5);
    } else if (tool == ToolId::TweakTool) {
        const double r = tweakGestureRadius(x0, y0, x1, y1);
        const QPointF v = viewMap.map(QPointF(x0, y0));
        const double s = std::max(std::abs(viewMap.m11()), std::abs(viewMap.m22()));
        painter.drawEllipse(v, r * s, r * s);
        painter.setBrush(blue);
        painter.drawEllipse(v, 3.0, 3.0);
    } else if (tool == ToolId::LpeTool) {
        painter.drawLine(viewMap.map(QPointF(x0, y0)), viewMap.map(QPointF(x1, y1)));
        const QPointF a = viewMap.map(QPointF(x0, y0));
        const QPointF b = viewMap.map(QPointF(x1, y1));
        painter.setBrush(Qt::white);
        painter.drawRect(QRectF(a.x() - 4, a.y() - 4, 8, 8));
        painter.drawRect(QRectF(b.x() - 4, b.y() - 4, 8, 8));
    } else if (tool == ToolId::ConnectorTool) {
        if (g_connRedrag && g_connLayer >= 0) {
            painter.drawLine(viewMap.map(g_connFixed), viewMap.map(QPointF(x1, y1)));
        } else {
            const QPointF a = viewMap.map(QPointF(x0, y0));
            const QPointF b = viewMap.map(QPointF(x1, y1));
            const QPointF elbow = viewMap.map(QPointF(x1, y0));
            painter.drawLine(a, elbow);
            painter.drawLine(elbow, b);
        }
    } else if (tool == ToolId::CalligraphyTool) {
        QPainterPath path;
        bool first = true;
        for (auto [px, py] : inkSpine_) {
            const QPointF v = viewMap.map(QPointF(px, py));
            if (first) {
                path.moveTo(v);
                first = false;
            } else {
                path.lineTo(v);
            }
        }
        painter.drawPath(path);
    } else if (tool == ToolId::Box3DTool) {
        vector::Box3D box;
        box.ox = x0;
        box.oy = y0;
        box.ex = x1 - x0;
        box.fx = 0;
        box.fy = y1 - y0;
        box.gx = -(x1 - x0) * 0.3;
        box.gy = -(y1 - y0) * 0.3;
        QPainterPath path;
        for (auto& segs : vector::boxToSegments(box)) {
            // Segments arrive as MoveTo/LineTo/Close chains.
            if (segs.kind == vector::Segment::Kind::MoveTo)
                path.moveTo(viewMap.map(QPointF(segs.x, segs.y)));
            else if (segs.kind == vector::Segment::Kind::LineTo)
                path.lineTo(viewMap.map(QPointF(segs.x, segs.y)));
            else if (segs.kind == vector::Segment::Kind::Close)
                path.closeSubpath();
        }
        painter.drawPath(path);
    } else if (tool == ToolId::MeshTool) {
        const QRectF r = viewMap.mapRect(
            QRectF(std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0),
                   std::abs(y1 - y0)));
        painter.drawRect(r);
        for (int k = 1; k < 3; k++) {
            painter.drawLine(QPointF(r.x() + r.width() * k / 3, r.y()),
                             QPointF(r.x() + r.width() * k / 3, r.y() + r.height()));
            painter.drawLine(QPointF(r.x(), r.y() + r.height() * k / 3),
                             QPointF(r.x() + r.width(), r.y() + r.height() * k / 3));
        }
    } else if (tool == ToolId::VectorEraserTool) {
        double w = state_->option(tool, QStringLiteral("veraser_width")).toDouble();
        if (!(w > 0)) w = 12.0;
        dot(x1, y1, w / 2);
    } else {
        // Pages / Marker: drag-rect outline.
        painter.drawRect(viewMap.mapRect(
            QRectF(std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0),
                   std::abs(y1 - y0))));
    }
}

}  // namespace pittore::ui
