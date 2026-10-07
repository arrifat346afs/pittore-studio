#include "ui/persona/vector_build.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>

#include "engine/vector/boolean.h"
#include "engine/vector/path.h"
#include "engine/vector/text_flow.h"
#include "engine/vector/vector_art.h"
#include "ui/app_state.h"
#include "ui/persona/vector_edit.h"
#include "ui/persona/vector_raster.h"
#include "ui/selection_mask.h"

namespace pittore::ui {
namespace {

using pittore::vector::ArtNode;
using pittore::vector::ArtPaint;
using pittore::vector::Segment;
using pittore::vector::BoolOp;
using pittore::vector::BoolFill;
using pittore::vector::booleanOp;
using Kind = Segment::Kind;

// Framed node from doc-space loops (MoveTo/LineTo/Close chains, even-odd
// so holes and pinched XOR loops fill right). `x0/y0` take the frame origin.
std::shared_ptr<pittore::vector::ArtNode> ringsToFramedNode(
    const std::vector<pittore::vector::BoolRing>& loops,
    const pittore::vector::ArtPaint& paint, const std::string& name,
    double* x0out, double* y0out) {
    auto node = std::make_shared<pittore::vector::ArtNode>();
    node->name = name;
    node->evenOdd = true;
    node->paint = paint;
    if (loops.empty()) return node;
    double x0 = loops.front().front().first, y0 = loops.front().front().second;
    double x1 = x0, y1 = y0;
    for (const auto& loop : loops)
        for (const auto& p : loop) {
            x0 = std::min(x0, p.first);
            y0 = std::min(y0, p.second);
            x1 = std::max(x1, p.first);
            y1 = std::max(y1, p.second);
        }
    for (const auto& loop : loops) {
        bool first = true;
        for (const auto& p : loop) {
            Segment s;
            s.kind = first ? Segment::Kind::MoveTo : Segment::Kind::LineTo;
            s.x = static_cast<float>(p.first - x0);
            s.y = static_cast<float>(p.second - y0);
            node->segments.push_back(s);
            first = false;
        }
        Segment c;
        c.kind = Segment::Kind::Close;
        node->segments.push_back(c);
    }
    if (x0out) *x0out = x0;
    if (y0out) *y0out = y0;
    return node;
}

// Rasterize a framed node into layer payloads (shared-tail math without the
// snapshot: the caller owns the undo bracket).
bool rasterizeFramedNode(std::shared_ptr<pittore::vector::ArtNode> node,
                         double x0, double y0, LayerItem& layer) {
    const double margin = node->paint.hasStroke
                              ? node->paint.strokeWidth / 2.0 + 1.0
                              : 1.0;
    const QRect trim = artNodePath(*node)
                           .boundingRect()
                           .adjusted(-margin, -margin, margin, margin)
                           .toAlignedRect();
    node->matrix[4] = -trim.x();
    node->matrix[5] = -trim.y();
    QImage img;
    QPointF trimCheck;
    if (!rasterizeArtNode(*node, &img, &trimCheck)) return false;
    auto pixels = straightRgba64ToImage(img);
    if (!pixels) return false;
    layer.pixels = std::move(pixels);
    layer.offset = QPointF(x0, y0) + QPointF(trim.x(), trim.y());
    layer.scaleX = layer.scaleY = 1.0;
    layer.art = std::move(node);
    ++layer.sourceStamp;
    return true;
}

}  // namespace

std::vector<Segment> painterPathToSegments(const QPainterPath& path) {
    std::vector<Segment> out;
    QPointF subStart;
    bool haveSub = false;
    auto closeIfLoop = [&](const QPointF& end) {
        if (haveSub &&
            std::hypot(end.x() - subStart.x(), end.y() - subStart.y()) <= 0.5) {
            Segment c;
            c.kind = Kind::Close;
            out.push_back(c);
            haveSub = false;
        }
    };
    const int n = path.elementCount();
    for (int i = 0; i < n; ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.type == QPainterPath::MoveToElement) {
            Segment s;
            s.kind = Kind::MoveTo;
            s.x = static_cast<float>(e.x);
            s.y = static_cast<float>(e.y);
            out.push_back(s);
            subStart = QPointF(e.x, e.y);
            haveSub = true;
        } else if (e.type == QPainterPath::LineToElement) {
            Segment s;
            s.kind = Kind::LineTo;
            s.x = static_cast<float>(e.x);
            s.y = static_cast<float>(e.y);
            out.push_back(s);
            closeIfLoop(QPointF(e.x, e.y));
        } else if (e.type == QPainterPath::CurveToElement) {
            // Cubic = CurveTo + 2 CurveToData elements.
            if (i + 2 >= n) break;
            const QPainterPath::Element d1 = path.elementAt(i + 1);
            const QPainterPath::Element d2 = path.elementAt(i + 2);
            Segment s;
            s.kind = Kind::CubicTo;
            s.c1x = static_cast<float>(e.x);
            s.c1y = static_cast<float>(e.y);
            s.c2x = static_cast<float>(d1.x);
            s.c2y = static_cast<float>(d1.y);
            s.x = static_cast<float>(d2.x);
            s.y = static_cast<float>(d2.y);
            out.push_back(s);
            closeIfLoop(QPointF(d2.x, d2.y));
            i += 2;
        }
    }
    return out;
}

std::vector<Segment> artSegmentsInDoc(const ArtNode& node,
                                      const LayerItem& layer) {
    const double* m = node.matrix;
    const QTransform docT =
        QTransform(m[0], m[1], m[2], m[3], m[4], m[5]) *
        QTransform().scale(layer.scaleX, layer.scaleY) *
        QTransform().translate(layer.offset.x(), layer.offset.y());
    std::vector<Segment> out;
    out.reserve(node.segments.size());
    for (auto s : node.segments) {
        auto map = [&](float x, float y, float* ox, float* oy) {
            const QPointF p = docT.map(QPointF(x, y));
            *ox = static_cast<float>(p.x());
            *oy = static_cast<float>(p.y());
        };
        if (s.kind == Kind::CubicTo) {
            map(s.c1x, s.c1y, &s.c1x, &s.c1y);
            map(s.c2x, s.c2y, &s.c2x, &s.c2y);
        }
        if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
            s.kind == Kind::CubicTo)
            map(s.x, s.y, &s.x, &s.y);
        out.push_back(s);
    }
    return out;
}

// Contiguous flood of the composite from `seed` (tolerance on RGB), as a
// grayscale mask the outline tracer consumes. Raw scanlines (no per-pixel
// QImage calls) plus an iteration fuse at 4× the pixel count, so a logic
// slip can degrade the fill but never hang the app.
QImage floodRegionMask(const QImage& composite, const QPoint& seed,
                       double tol) {
    QImage img = composite.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    QImage mask(w, h, QImage::Format_Grayscale8);
    mask.fill(0);
    if (w < 1 || h < 1 || !img.rect().contains(seed)) return mask;
    const QRgb target = img.pixel(seed);
    const int tr = qRed(target), tg = qGreen(target), tb = qBlue(target);
    const double tol2 = tol * tol;
    std::vector<char> seen(static_cast<std::size_t>(w) * h, 0);
    std::vector<QPoint> stack;
    stack.reserve(1024);
    auto paint = [&](const QPoint& q) { mask.scanLine(q.y())[q.x()] = 255; };
    stack.push_back(seed);
    seen[static_cast<std::size_t>(seed.y()) * w + seed.x()] = 1;
    paint(seed);
    std::size_t fuse = static_cast<std::size_t>(w) * h * 4 + 64;
    while (!stack.empty() && fuse-- > 0) {
        const QPoint p = stack.back();
        stack.pop_back();
        const QPoint nb[] = {{p.x() + 1, p.y()},
                             {p.x() - 1, p.y()},
                             {p.x(), p.y() + 1},
                             {p.x(), p.y() - 1}};
        for (const QPoint& q : nb) {
            if (q.x() < 0 || q.y() < 0 || q.x() >= w || q.y() >= h) continue;
            const std::size_t qi = static_cast<std::size_t>(q.y()) * w + q.x();
            if (seen[qi]) continue;
            seen[qi] = 1;
            const QRgb px = img.pixel(q.x(), q.y());
            const double dr = qRed(px) - tr, dg = qGreen(px) - tg,
                         db = qBlue(px) - tb;
            if (dr * dr + dg * dg + db * db > tol2) continue;
            paint(q);
            stack.push_back(q);
        }
    }
    return mask;
}

bool AppState::floodFillVectorArt(const QPointF& docPos) {
    DocumentItem* d = activeDocument();
    if (!d || d->size.isEmpty() || d->composite.isNull()) return false;
    const ToolId tool = ToolId::VectorFloodFillTool;
    const int fillMode = option(tool, QStringLiteral("fill_mode")).toInt();
    const bool visibleBounds =
        option(tool, QStringLiteral("visible_bounds")).toBool();
    // Smart refill repaints the hit art instead of adding a layer.
    if (fillMode == 1) {
        const int hit = topPixelLayerAt(*d, docPos);
        const LayerItem* l =
            (hit >= 0 && hit < d->layers.size()) ? &d->layers[hit] : nullptr;
        if (!l || !l->art || l->art->isEmpty()) {
            setStatusHint(tr("Smart refill: click a vector shape."));
            return false;
        }
        auto paint = l->art->paint;
        const QColor fg = foreground();
        paint.hasFill = true;
        paint.fill[0] = static_cast<std::uint8_t>(fg.red());
        paint.fill[1] = static_cast<std::uint8_t>(fg.green());
        paint.fill[2] = static_cast<std::uint8_t>(fg.blue());
        paint.fill[3] = static_cast<std::uint8_t>(fg.alpha());
        return applyVectorPaint(hit, paint, -1.0, tr("Smart Refill"));
    }
    std::vector<Segment> segs;
    QRectF regionDoc;
    if (!visibleBounds) {
        // Whole canvas.
        QPainterPath rect;
        rect.addRect(QRectF(QPointF(0, 0), QSizeF(d->size)));
        segs = painterPathToSegments(rect);
        regionDoc = QRectF(QPointF(0, 0), QSizeF(d->size));
    } else {
        const QPoint seed(qBound(0, qRound(docPos.x()), d->composite.width() - 1),
                          qBound(0, qRound(docPos.y()), d->composite.height() - 1));
        const QImage mask = floodRegionMask(d->composite, seed, 32.0);
        const QPainterPath traced = selectionOutlineFromMask(
            mask, mask.rect(), 127);
        if (traced.isEmpty()) {
            setStatusHint(tr("Flood fill: nothing to fill there."));
            return false;
        }
        segs = painterPathToSegments(traced);
        regionDoc = traced.boundingRect();
    }
    if (segs.empty()) {
        setStatusHint(tr("Flood fill: nothing to fill there."));
        return false;
    }
    // Frame + commit through the shared tail (fill = foreground).
    // Bounds cover handle tips too, and the reframe moves handles with
    // their endpoints (unmoved handles would warp every curve).
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    auto eat = [&](double x, double y) {
        if (!any) {
            x0 = x1 = x;
            y0 = y1 = y;
            any = true;
        } else {
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    };
    for (const auto& s : segs) {
        if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
            s.kind == Kind::CubicTo) {
            eat(s.x, s.y);
        }
        if (s.kind == Kind::CubicTo) {
            eat(s.c1x, s.c1y);
            eat(s.c2x, s.c2y);
        }
    }
    if (!any) return false;
    auto node = std::make_shared<ArtNode>();
    node->name = toolName(tool).toStdString();
    node->segments.reserve(segs.size());
    for (auto s : segs) {
        if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
            s.kind == Kind::CubicTo) {
            s.x = static_cast<float>(s.x - x0);
            s.y = static_cast<float>(s.y - y0);
        }
        if (s.kind == Kind::CubicTo) {
            s.c1x = static_cast<float>(s.c1x - x0);
            s.c1y = static_cast<float>(s.c1y - y0);
            s.c2x = static_cast<float>(s.c2x - x0);
            s.c2y = static_cast<float>(s.c2y - y0);
        }
        node->segments.push_back(s);
    }
    const QColor fg = foreground();
    node->paint.hasFill = true;
    node->paint.fill[0] = static_cast<std::uint8_t>(fg.red());
    node->paint.fill[1] = static_cast<std::uint8_t>(fg.green());
    node->paint.fill[2] = static_cast<std::uint8_t>(fg.blue());
    node->paint.fill[3] = static_cast<std::uint8_t>(fg.alpha());
    if (!commitArtNodeLayer(std::move(node),
                            QRectF(QPointF(x0, y0), QPointF(x1, y1)), tool,
                            tr("Flood Fill"), true))
        return false;
    // Knockout deletes art fully inside the filled region.
    if (fillMode == 2 && d) {
        QVector<int> kill;
        for (int i = 0; i < d->layers.size(); ++i) {
            const LayerItem& l = d->layers[i];
            if (!l.art || l.art->isEmpty() || l.locked) continue;
            if (i == d->activeLayer) continue;  // the fresh fill
            if (regionDoc.contains(layerBounds(*d, l))) kill.push_back(i);
        }
        if (!kill.isEmpty()) {
            d->beginUndoAction();
            std::sort(kill.begin(), kill.end(), std::greater<int>());
            for (int idx : kill) d->layers.removeAt(idx);
            d->selectedLayers.clear();
            d->activeLayer =
                qBound(0, d->activeLayer, qMax(0, d->layers.size() - 1));
            d->commitUndoAction(tr("Flood Fill"), QStringLiteral("flood-fill"));
            d->rebuildComposite();
            emit layersChanged();
            emit historyChanged();
            emit documentModified(d);
        }
    }
    return true;
}

bool AppState::shapeBuilderAt(const QRectF& docRect, int action) {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    // Intersected art layers (click = topmost at the point).
    const bool click = docRect.width() < 4.0 && docRect.height() < 4.0;
    QVector<int> hits;
    if (click) {
        const int hit = topPixelLayerAt(*d, docRect.center());
        if (hit >= 0 && d->layers[hit].art &&
            !d->layers[hit].art->isEmpty())
            hits.push_back(hit);
    } else {
        for (int i = 0; i < d->layers.size(); ++i) {
            const LayerItem& l = d->layers[i];
            if (!l.visible || !l.art || l.art->isEmpty()) continue;
            if (layerBounds(*d, l).intersects(docRect)) hits.push_back(i);
        }
    }
    if (action == 2) {
        // Create: each art's overlap with the union of the rest becomes a
        // new layer. Sources are untouched (locked rows still contribute
        // geometry).
        if (hits.size() < 2) {
            setStatusHint(tr("Shape Builder: drag across overlapping shapes."));
            return false;
        }
        struct Piece {
            std::vector<pittore::vector::BoolRing> loops;
            pittore::vector::ArtPaint paint;
        };
        std::vector<std::vector<pittore::vector::BoolRing>> allRings;
        for (int i : hits) {
            const LayerItem& l = d->layers[i];
            allRings.push_back(artToRings(*l.art, l));
        }
        std::vector<Piece> pieces;
        for (std::size_t i = 0; i < allRings.size(); ++i) {
            const LayerItem& l = d->layers[hits[(int)i]];
            std::vector<pittore::vector::BoolRing> rest;
            bool restFirst = true;
            for (std::size_t k = 0; k < allRings.size(); ++k) {
                if (k == i || allRings[k].empty()) continue;
                if (restFirst) {
                    rest = allRings[k];
                    restFirst = false;
                } else {
                    rest = booleanOp(rest, allRings[k], BoolOp::Union,
                                     l.art->evenOdd ? BoolFill::EvenOdd
                                                    : BoolFill::NonZero);
                }
            }
            if (restFirst) continue;
            auto inter = booleanOp(
                allRings[i], rest, BoolOp::Intersection,
                l.art->evenOdd ? BoolFill::EvenOdd : BoolFill::NonZero);
            if (!inter.empty())
                pieces.push_back(Piece{std::move(inter), l.art->paint});
        }
        if (pieces.empty()) {
            setStatusHint(tr("Shape Builder: the shapes do not overlap."));
            return false;
        }
        // Dedupe identical overlap zones (each art reports the same shared
        // region): same quantized bbox + area keeps one copy.
        struct PieceKey {
            long long x0, y0, x1, y1, a;
            bool operator==(const PieceKey& o) const {
                return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1 &&
                       a == o.a;
            }
        };
        struct PieceKeyHash {
            std::size_t operator()(const PieceKey& k) const {
                std::size_t h = std::hash<long long>()(k.x0);
                h = h * 31 + std::hash<long long>()(k.y0);
                h = h * 31 + std::hash<long long>()(k.x1);
                h = h * 31 + std::hash<long long>()(k.y1);
                return h * 31 + std::hash<long long>()(k.a);
            }
        };
        auto pieceKey = [](const std::vector<pittore::vector::BoolRing>& loops) {
            double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300, a = 0.0;
            for (const auto& loop : loops) {
                a += std::abs(pittore::vector::boolRingArea(loop));
                for (const auto& p : loop) {
                    x0 = std::min(x0, p.first);
                    y0 = std::min(y0, p.second);
                    x1 = std::max(x1, p.first);
                    y1 = std::max(y1, p.second);
                }
            }
            const double q = 100.0;  // 0.01 doc-px quantization
            return PieceKey{(long long)std::llround(x0 * q),
                            (long long)std::llround(y0 * q),
                            (long long)std::llround(x1 * q),
                            (long long)std::llround(y1 * q),
                            (long long)std::llround(a * q)};
        };
        std::unordered_set<PieceKey, PieceKeyHash> seen;
        std::vector<Piece> unique;
        for (auto& piece : pieces) {
            if (seen.insert(pieceKey(piece.loops)).second)
                unique.push_back(std::move(piece));
        }
        pieces.swap(unique);
        d->beginUndoAction();
        for (auto& piece : pieces) {
            double x0 = 0.0, y0 = 0.0;
            auto node = ringsToFramedNode(piece.loops, piece.paint,
                                          toolName(ToolId::ShapeBuilderTool)
                                              .toStdString(),
                                          &x0, &y0);
            LayerItem layer;
            layer.name = toolName(ToolId::ShapeBuilderTool);
            layer.kind = LayerItem::Kind::Pixel;
            if (!rasterizeFramedNode(node, x0, y0, layer)) continue;
            layer.thumbnail = QImage();
            bakeArtDense(layer, d->zoom);
            addLayer(std::move(layer));
        }
        d->commitUndoAction(tr("Shape Builder"), QStringLiteral("shape-builder"));
        d->rebuildComposite();
        emit layersChanged();
        emit historyChanged();
        emit documentModified(d);
        setStatusHint(tr("Shape Builder: created."));
        return true;
    }
    // Locked rows never join or leave.
    QVector<int> usable;
    for (int i : hits)
        if (!d->layers[i].locked) usable.push_back(i);
    if (usable.isEmpty()) {
        setStatusHint(action == 0 ? tr("Shape Builder: drag across vector shapes.")
                                  : tr("Shape Builder: nothing to delete there."));
        return false;
    }
    std::sort(usable.begin(), usable.end());  // top-first (index 0 is top)
    d->beginUndoAction();
    if (action == 1) {
        for (auto it = usable.crbegin(); it != usable.crend(); ++it)
            d->layers.removeAt(*it);
        d->selectedLayers.clear();
        d->activeLayer =
            d->layers.isEmpty() ? 0 : qBound(0, d->activeLayer, d->layers.size() - 1);
        d->commitUndoAction(tr("Shape Builder"), QStringLiteral("shape-builder"));
    } else {
        // Add: concatenate bottom-to-top (reverse index order — 0 is top)
        // so paint order survives inside the single combined node.
        auto node = std::make_shared<ArtNode>();
        node->name = toolName(ToolId::ShapeBuilderTool).toStdString();
        const bool firstStyle =
            option(ToolId::ShapeBuilderTool, QStringLiteral("first_style")).toBool();
        const LayerItem& first = d->layers[usable.front()];
        if (firstStyle && first.art)
            node->paint = first.art->paint;
        else {
            const QColor fg = foreground();
            node->paint.hasFill = true;
            node->paint.fill[0] = static_cast<std::uint8_t>(fg.red());
            node->paint.fill[1] = static_cast<std::uint8_t>(fg.green());
            node->paint.fill[2] = static_cast<std::uint8_t>(fg.blue());
            node->paint.fill[3] = static_cast<std::uint8_t>(fg.alpha());
        }
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool any = false;
        std::vector<Segment> docSegs;
        for (auto it = usable.crbegin(); it != usable.crend(); ++it) {
            const LayerItem& l = d->layers[*it];
            for (auto s : artSegmentsInDoc(*l.art, l)) {
                if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
                    s.kind == Kind::CubicTo) {
                    if (!any) {
                        x0 = x1 = s.x;
                        y0 = y1 = s.y;
                        any = true;
                    } else {
                        x0 = std::min(x0, (double)s.x);
                        y0 = std::min(y0, (double)s.y);
                        x1 = std::max(x1, (double)s.x);
                        y1 = std::max(y1, (double)s.y);
                    }
                }
                if (s.kind == Kind::CubicTo) {
                    x0 = std::min({x0, (double)s.c1x, (double)s.c2x});
                    y0 = std::min({y0, (double)s.c1y, (double)s.c2y});
                    x1 = std::max({x1, (double)s.c1x, (double)s.c2x});
                    y1 = std::max({y1, (double)s.c1y, (double)s.c2y});
                }
                docSegs.push_back(s);
            }
        }
        bool ok = any;
        // Reframe endpoints and controls into the local frame.
        node->segments.reserve(docSegs.size());
        for (auto s : docSegs) {
            if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
                s.kind == Kind::CubicTo) {
                s.x = static_cast<float>(s.x - x0);
                s.y = static_cast<float>(s.y - y0);
            }
            if (s.kind == Kind::CubicTo) {
                s.c1x = static_cast<float>(s.c1x - x0);
                s.c1y = static_cast<float>(s.c1y - y0);
                s.c2x = static_cast<float>(s.c2x - x0);
                s.c2y = static_cast<float>(s.c2y - y0);
            }
            node->segments.push_back(s);
        }
        for (auto it = usable.crbegin(); it != usable.crend(); ++it)
            d->layers.removeAt(*it);
        d->selectedLayers.clear();
        // Rasterize the framed node (shared-tail math, inline: the tail
        // commits its own snapshot, which cannot nest here).
        const double margin = node->paint.hasStroke
                                  ? node->paint.strokeWidth / 2.0 + 1.0
                                  : 1.0;
        const QRect trim = artNodePath(*node)
                               .boundingRect()
                               .adjusted(-margin, -margin, margin, margin)
                               .toAlignedRect();
        node->matrix[4] = -trim.x();
        node->matrix[5] = -trim.y();
        QImage img;
        QPointF trimCheck;
        if (!rasterizeArtNode(*node, &img, &trimCheck)) ok = false;
        auto px = ok ? straightRgba64ToImage(img) : nullptr;
        if (!px) ok = false;
        if (ok) {
            LayerItem combined;
            combined.name = toolName(ToolId::ShapeBuilderTool);
            combined.kind = LayerItem::Kind::Pixel;
            combined.pixels = std::move(px);
            combined.offset = QPointF(x0, y0) + QPointF(trim.x(), trim.y());
            combined.scaleX = combined.scaleY = 1.0;
            combined.art = std::move(node);
            ++combined.sourceStamp;
            bakeArtDense(combined, d->zoom);
            addLayer(std::move(combined));
        }
        d->activeLayer =
            d->layers.isEmpty() ? 0 : qBound(0, d->activeLayer, d->layers.size() - 1);
        d->commitUndoAction(tr("Shape Builder"), QStringLiteral("shape-builder"));
        if (!ok) {
            setStatusHint(tr("Could not render the combined shape."));
            return false;
        }
    }
    d->rebuildComposite();
    emit layersChanged();
    emit historyChanged();
    emit documentModified(d);
    setStatusHint(action == 1 ? tr("Shape Builder: deleted.")
                              : tr("Shape Builder: combined."));
    return true;
}

std::vector<pittore::vector::BoolRing> artToRings(
    const pittore::vector::ArtNode& node, const LayerItem& layer) {
    using pittore::vector::BoolRing;
    const std::vector<pittore::vector::Segment> doc =
        artSegmentsInDoc(node, layer);
    pittore::vector::PathBuilder b;
    bool open = true;
    for (const auto& s : doc) {
        switch (s.kind) {
            case pittore::vector::Segment::Kind::MoveTo:
                b.moveTo(s.x, s.y);
                open = false;
                break;
            case pittore::vector::Segment::Kind::LineTo:
                if (open) {
                    b.moveTo(s.x, s.y);
                    open = false;
                } else {
                    b.lineTo(s.x, s.y);
                }
                break;
            case pittore::vector::Segment::Kind::CubicTo:
                if (open) {
                    b.moveTo(s.c1x, s.c1y);
                    open = false;
                }
                b.cubicTo(s.c1x, s.c1y, s.c2x, s.c2y, s.x, s.y);
                break;
            case pittore::vector::Segment::Kind::Close:
                b.close();
                open = true;
                break;
        }
    }
    const pittore::vector::Path flat = b.build(0.25f);
    std::vector<BoolRing> rings;
    for (const auto& sub : flat.subpaths) {
        if (sub.size() < 3) continue;
        BoolRing ring;
        ring.reserve(sub.size());
        for (const auto& p : sub) ring.emplace_back(p.first, p.second);
        rings.push_back(std::move(ring));
    }
    return rings;
}

bool AppState::booleanFoldLayers(const QVector<int>& indices,
                                 pittore::vector::BoolOp op,
                                 const std::vector<pittore::vector::BoolRing>* extra,
                                 const QString& undoName) {
    using pittore::vector::BoolFill;
    using pittore::vector::BoolRing;
    DocumentItem* d = activeDocument();
    if (!d) return false;
    // Usable targets: distinct, unlocked art layers, top-first.
    QVector<int> usable;
    for (int i : indices) {
        if (i < 0 || i >= d->layers.size()) continue;
        const LayerItem& l = d->layers[i];
        if (l.locked || !l.art || l.art->isEmpty()) continue;
        if (!usable.contains(i)) usable.push_back(i);
    }
    std::sort(usable.begin(), usable.end());
    if (usable.size() + (extra && !extra->empty() ? 1 : 0) < 2) {
        setStatusHint(tr("Select two vector shapes to combine."));
        return false;
    }
    const LayerItem& first = d->layers[usable.front()];
    const BoolFill fill =
        first.art->evenOdd ? BoolFill::EvenOdd : BoolFill::NonZero;
    // Fold pairwise (compute BEFORE the snapshot: nothing mutates yet).
    std::vector<BoolRing> acc = artToRings(*first.art, first);
    for (int k = 1; k < usable.size(); ++k) {
        const LayerItem& l = d->layers[usable[k]];
        acc = booleanOp(acc, artToRings(*l.art, l), op, fill);
        if (acc.empty() &&
            (op == BoolOp::Intersection || op == BoolOp::Difference))
            break;  // empty stays empty for these ops
    }
    if (extra && !extra->empty() && !acc.empty())
        acc = booleanOp(acc, *extra, op, fill);
    else if (extra && !extra->empty() && acc.empty() &&
             (op == BoolOp::Union || op == BoolOp::Xor))
        acc = *extra;
    if (acc.empty()) {
        setStatusHint(tr("The shapes do not overlap."));
        return false;
    }
    double x0 = 0.0, y0 = 0.0;
    auto node = ringsToFramedNode(acc, first.art->paint,
                                  first.name.toStdString(), &x0, &y0);
    // Every replaced/removed layer's old bounds joins the dirty region:
    // recompositing only the survivor's footprint would leave the removed
    // layers' pixels stale on screen.
    QRectF dirtyBounds = layerBounds(*d, d->layers[usable.front()]);
    for (int k = 1; k < usable.size(); ++k)
        dirtyBounds |= layerBounds(*d, d->layers[usable[k]]);
    d->beginUndoAction();
    LayerItem& l = d->layers[usable.front()];
    l.name = first.name;
    if (!rasterizeFramedNode(node, x0, y0, l)) {
        // Snapshot rolls back on undo; close the bracket honestly.
        d->commitUndoAction(undoName, QStringLiteral("boolean"));
        setStatusHint(tr("Could not render the combined shape."));
        return false;
    }
    l.thumbnail = QImage();
    l.styledValid = false;
    bakeArtDense(l, d->zoom);
    for (auto it = usable.crbegin(); it != usable.crend(); ++it) {
        if (*it != usable.front()) d->layers.removeAt(*it);
    }
    d->selectedLayers.clear();
    d->activeLayer = qBound(0, d->activeLayer, qMax(0, d->layers.size() - 1));
    const QRectF newBounds = layerBounds(*d, d->layers[usable.front()]);
    const QRect region =
        (dirtyBounds | newBounds)
            .toAlignedRect()
            .intersected(QRect(QPoint(0, 0), d->size));
    d->dirty = true;
    if (region.isEmpty())
        d->rebuildComposite();
    else
        d->recompositeRegion(region);
    d->commitUndoAction(undoName.isEmpty() ? tr("Combine") : undoName,
                        QStringLiteral("boolean"));
    emit historyChanged();
    emit layersChanged();
    emit documentModified(d);
    return true;
}

bool AppState::textOnPath(const QString& text, const QString& family,
                           double sizePt) {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    if (text.isEmpty()) {
        setStatusHint(tr("Text on Path: enter some text first."));
        return false;
    }
    const int index = vectorEditableLayer(this);
    const LayerItem* l =
        (index >= 0 && index < d->layers.size()) ? &d->layers[index] : nullptr;
    if (!l || !l->art || l->art->isEmpty()) {
        setStatusHint(tr("Text on Path: select a vector shape layer."));
        return false;
    }
    // Flatten the longest outline (open or closed) to walk along.
    pittore::vector::PathBuilder b;
    bool open = true;
    for (const Segment& s : l->art->segments) {
        switch (s.kind) {
            case Kind::MoveTo:
                b.moveTo(s.x, s.y);
                open = false;
                break;
            case Kind::LineTo:
                if (open) {
                    b.moveTo(s.x, s.y);
                    open = false;
                } else {
                    b.lineTo(s.x, s.y);
                }
                break;
            case Kind::CubicTo:
                if (open) {
                    b.moveTo(s.c1x, s.c1y);
                    open = false;
                }
                b.cubicTo(s.c1x, s.c1y, s.c2x, s.c2y, s.x, s.y);
                break;
            case Kind::Close:
                b.close();
                open = true;
                break;
        }
    }
    const pittore::vector::Path flattened = b.build(0.5f);
    const std::vector<std::pair<float, float>>* best = nullptr;
    double bestLen = 0.0;
    for (const auto& sub : flattened.subpaths) {
        if (sub.size() < 2) continue;
        double len = 0.0;
        for (std::size_t i = 1; i < sub.size(); ++i)
            len += std::hypot(sub[i].first - sub[i - 1].first,
                              sub[i].second - sub[i - 1].second);
        if (len > bestLen) {
            bestLen = len;
            best = &sub;
        }
    }
    if (!best || !(bestLen > 1.0)) {
        setStatusHint(tr("Text on Path: the outline is too short."));
        return false;
    }
    QFont font(family.isEmpty() ? QFont().family() : family);
    font.setPixelSize(qMax(1, qRound(sizePt * d->dpi / 72.0)));
    const QFontMetricsF fm(font);
    // Engine arclength walk (text_flow): one advance per codepoint so
    // shaping, tracking and clipping behave identically in import and UI.
    std::vector<std::pair<double, double>> spine;
    for (const auto& pt : *best) spine.emplace_back(pt.first, pt.second);
    std::string utf8;
    std::vector<double> advances;
    for (int i = 0; i < text.size();) {
        uint code = text[i].unicode();
        int len = 1;
        if (text[i].isHighSurrogate() && i + 1 < text.size() &&
            text[i + 1].isLowSurrogate()) {
            code = QChar::surrogateToUcs4(text[i].unicode(), text[i + 1].unicode());
            len = 2;
        }
        if (code == '\n' || code == '\t') {
            i += len;
            continue;
        }
        const QChar ch[2] = {text[i], len > 1 ? text[i + 1] : QChar()};
        advances.push_back(fm.horizontalAdvance(QString(ch, len)));
        const QByteArray bytes = QString(ch, len).toUtf8();
        utf8.append(bytes.constData(), (size_t)bytes.size());
        i += len;
    }
    if (utf8.empty()) {
        setStatusHint(tr("Text on Path: enter some text first."));
        return false;
    }
    const auto glyphs =
        pittore::vector::textOnPathLayout(utf8, spine, advances, 0.0, 0.0);
    QPainterPath all;
    bool clipped = false;
    for (const auto& g : glyphs) {
        if (!g.visible) {
            clipped = true;
            continue;
        }
        QPainterPath glyph;
        glyph.addText(0, 0, font, QString::fromUcs4(&g.ch, 1));
        QTransform t;
        t.translate(g.x, g.y);
        t.rotate(g.rotationDeg);
        all.addPath(t.map(glyph));
    }
    if (all.isEmpty()) {
        setStatusHint(tr("Text on Path: no glyphs fit the outline."));
        return false;
    }
    const std::vector<Segment> segs = painterPathToSegments(all);
    if (segs.empty()) {
        setStatusHint(tr("Text on Path: no glyphs fit the outline."));
        return false;
    }
    // Frame + commit (glyph counters punch holes: even-odd).
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    for (const auto& s : segs) {
        if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
            s.kind == Kind::CubicTo) {
            if (!any) {
                x0 = x1 = s.x;
                y0 = y1 = s.y;
                any = true;
            } else {
                x0 = std::min(x0, (double)s.x);
                y0 = std::min(y0, (double)s.y);
                x1 = std::max(x1, (double)s.x);
                y1 = std::max(y1, (double)s.y);
            }
        }
    }
    if (!any) return false;
    auto node = std::make_shared<ArtNode>();
    node->name = toolName(ToolId::HorizontalType).toStdString();
    node->evenOdd = true;
    node->segments.reserve(segs.size());
    for (auto s : segs) {
        if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
            s.kind == Kind::CubicTo) {
            s.x = static_cast<float>(s.x - x0);
            s.y = static_cast<float>(s.y - y0);
        }
        if (s.kind == Kind::CubicTo) {
            s.c1x = static_cast<float>(s.c1x - x0);
            s.c1y = static_cast<float>(s.c1y - y0);
            s.c2x = static_cast<float>(s.c2x - x0);
            s.c2y = static_cast<float>(s.c2y - y0);
        }
        node->segments.push_back(s);
    }
    QColor color = option(ToolId::HorizontalType, QStringLiteral("color"))
                       .value<QColor>();
    if (!color.isValid()) color = foreground();
    node->paint.hasFill = true;
    node->paint.fill[0] = static_cast<std::uint8_t>(color.red());
    node->paint.fill[1] = static_cast<std::uint8_t>(color.green());
    node->paint.fill[2] = static_cast<std::uint8_t>(color.blue());
    node->paint.fill[3] = static_cast<std::uint8_t>(color.alpha());
    if (!commitArtNodeLayer(std::move(node),
                            QRectF(QPointF(x0, y0), QPointF(x1, y1)),
                            ToolId::HorizontalType, tr("Text on Path"), true))
        return false;
    if (clipped)
        setStatusHint(tr("Text on Path: longer than the outline (clipped)."));
    return true;
}

bool AppState::textInShape(const QString& text, const QString& family,
                           double sizePt) {
    DocumentItem* d = activeDocument();
    if (!d) return false;
    if (text.isEmpty()) {
        setStatusHint(tr("Text in Shape: enter some text first."));
        return false;
    }
    const int index = vectorEditableLayer(this);
    const LayerItem* l =
        (index >= 0 && index < d->layers.size()) ? &d->layers[index] : nullptr;
    if (!l || !l->art || l->art->isEmpty()) {
        setStatusHint(tr("Text in Shape: select a vector shape layer."));
        return false;
    }
    // Frame columns from the art bounds (document space, single column).
    pittore::vector::Path flat = pittore::vector::flattenSegments(l->art->segments, 0.5f);
    double x0 = 1e100, y0 = 1e100, x1 = -1e100, y1 = -1e100;
    for (const auto& sp : flat.subpaths)
        for (const auto& pt : sp) {
            double wx = l->art->matrix[0] * pt.first + l->art->matrix[2] * pt.second +
                        l->art->matrix[4];
            double wy = l->art->matrix[1] * pt.first + l->art->matrix[3] * pt.second +
                        l->art->matrix[5];
            x0 = std::min(x0, wx);
            y0 = std::min(y0, wy);
            x1 = std::max(x1, wx);
            y1 = std::max(y1, wy);
        }
    if (!(x1 > x0 && y1 > y0)) return false;
    QFont font(family.isEmpty() ? QFont().family() : family);
    font.setPixelSize(qMax(1, qRound(sizePt * d->dpi / 72.0)));
    const QFontMetricsF fm(font);
    std::vector<double> advances;
    for (const QChar& ch : text) advances.push_back(fm.horizontalAdvance(ch));
    std::vector<std::array<double, 4>> cols{{x0, y0, x1, y1}};
    const auto glyphs = pittore::vector::textInShapeLayout(
        text.toStdString(), cols, advances, font.pixelSize() * 1.25, true);
    QPainterPath all;
    size_t gi = 0;
    for (int ci = 0; ci < text.size() && gi < glyphs.size();) {
        const auto& g = glyphs[gi++];
        const QChar ch = text[ci++];
        if (!g.visible || ch == QLatin1Char('\n')) continue;
        QPainterPath one;
        one.addText(g.x, g.y, font, QString(ch));
        all.addPath(one);
    }
    if (all.isEmpty()) {
        setStatusHint(tr("Text in Shape: no glyphs fit the shape."));
        return false;
    }
    const std::vector<Segment> segs = painterPathToSegments(all);
    if (segs.empty()) return false;
    auto node = std::make_shared<ArtNode>();
    node->name = toolName(ToolId::HorizontalType).toStdString();
    node->evenOdd = true;
    const QRectF bounds = all.boundingRect();
    for (auto s : segs) {
        if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
            s.kind == Kind::CubicTo) {
            s.x = static_cast<float>(s.x - bounds.x());
            s.y = static_cast<float>(s.y - bounds.y());
        }
        if (s.kind == Kind::CubicTo) {
            s.c1x = static_cast<float>(s.c1x - bounds.x());
            s.c1y = static_cast<float>(s.c1y - bounds.y());
            s.c2x = static_cast<float>(s.c2x - bounds.x());
            s.c2y = static_cast<float>(s.c2y - bounds.y());
        }
        node->segments.push_back(s);
    }
    QColor color = option(ToolId::HorizontalType, QStringLiteral("color"))
                       .value<QColor>();
    if (!color.isValid()) color = foreground();
    node->paint.hasFill = true;
    node->paint.fill[0] = static_cast<std::uint8_t>(color.red());
    node->paint.fill[1] = static_cast<std::uint8_t>(color.green());
    node->paint.fill[2] = static_cast<std::uint8_t>(color.blue());
    node->paint.fill[3] = static_cast<std::uint8_t>(color.alpha());
    return commitArtNodeLayer(std::move(node), bounds, ToolId::HorizontalType,
                              tr("Text in Shape"), true);
}

}  // namespace pittore::ui
