#pragma once
// Crisp vector overlay: which layers may paint straight from geometry over
// the composite blit, and the painter that does it.
//
// planCrispOverlay reuses the vetted drawable set (same contract as the
// live segmented walk: effectively visible vector layers, opaque, Normal
// blend, no masks/styles/filters) but qualifies only layers with no
// above-pixel overlap anywhere on screen. No partial clipping: a layer is
// either globally safe (paints whole) or stays raster-only. That makes the
// result tileable - any sub-rect of the plan paints correctly on its own -
// which is what the background tile baker relies on.
//
// drawCrispLayer paints one planned layer. It never touches per-layer
// mutable caches, so it is safe to call from worker threads; all inputs
// are immutable geometry plus plain placement values.
#include <QRectF>
#include <QVector>
#include <QPainterPath>

#include <vector>

class QPainter;
class QRect;
class QTransform;

namespace pittore::vector {
struct ArtNode;
}

namespace pittore::ui {

struct DocumentItem;
struct LayerItem;

// Live-bake budget per frame, milliseconds: the segmented walk rasterizes
// crisp layers only while time remains; the rest keep composite pixels and
// sharpen via background tiles. Keeps huge-vector frames interactive while
// zooming; settled views fill in behind within a frame or two.
constexpr int kMaxCrispBakeMs = 25;

// Quarter-stop zoom quantization shared by the view cache and the tile
// store: nearby fractional zooms share one baked raster instead of
// re-baking per frame.
double crispZoomBucket(double zoom);

// Per-frame vector-overlay counters, filled by the cache-fed paint path so
// the frame log can tell blits apart from rebakes.
struct CrispStats {
    long drawn = 0;    // crisp layers painted this frame
    long hits = 0;     // served from the view cache
    long baked = 0;    // rasterized and stored this frame
    long direct = 0;   // too big to keep: drawn without caching
    long skipped = 0;  // cache miss past the bake deadline: composite shows
                       // this frame, background tiles fill it in behind
};

// Planned layer indices, bottom-to-top (paint order). Empty visibleDoc
// means the whole document: nothing is culled then.
QVector<int> planCrispOverlay(const DocumentItem& doc,
                              const QRectF& visibleDoc);

// Document-space paint box of one layer (stroke-padded, doc-clipped).
// Marker layers and layers without a bake box fall back to the document.
QRectF crispLayerBox(const DocumentItem& doc, int index);

// Above-pixel coverage index: which content runs may overlap a box.
// A linear per-layer/per-run scan turns quadratic past a few thousand
// layers, so runs live in a doc-space grid and queries only visit runs
// sharing a cell with the box. Full-document runs match everything.
struct CoverRun {
    QRectF box;
    bool full = false;  // unbounded: overlaps everything below it
};
class CoverIndex {
public:
    void build(const std::vector<CoverRun>& runs, const QRectF& docRect);
    // Indices into the built run list that may overlap `box`.
    void query(const QRectF& box, std::vector<int>& out) const;

private:
    std::vector<CoverRun> runs_;
    QRectF docRect_;
    double cell_ = 128.0;
    int cols_ = 0;
    int rows_ = 0;
    std::vector<std::vector<int>> cells_;
    std::vector<int> fullRuns_;
    mutable std::vector<int> seen_;
    mutable int stamp_ = 0;
};

// Paint one planned layer's vector content. The painter transform must map
// document space; node placement applies inside. Returns false when the
// layer draws nothing.
bool drawCrispLayer(QPainter& p, const DocumentItem& doc, int index);

// Worker-thread core behind drawCrispLayer: same paint from explicit
// placement values, no document access at all.
bool drawCrispArt(QPainter& p, const pittore::vector::ArtNode& node,
                  double scaleX, double scaleY, double offX, double offY);

// Resolution-independent geometry for one node: the fill path plus the
// pre-expanded stroke outline (or a pen fallback when degenerate). Built
// once and shared across every tile that paints the node, instead of
// once per tile per bucket.
struct CrispPaths {
    QPainterPath fill;
    QPainterPath stroke;
    // True when stroke paints through the pen path (degenerate outline).
    bool penStroke = false;
    bool empty = true;
};

CrispPaths buildCrispPaths(const pittore::vector::ArtNode& node);

// Paint from prebuilt geometry: byte-identical to drawCrispArt for the
// same node and placement.
bool drawCrispPaths(QPainter& p, const CrispPaths& paths,
                    const pittore::vector::ArtNode& node, double scaleX,
                    double scaleY, double offX, double offY);

// Hairline contour of one vector layer for outline mode. Cosmetic pen:
// one raster pixel at whatever density the caller bakes.
void drawOutlineLayer(QPainter& p, const DocumentItem& doc, int index);

// Outline-mode content: hairline contours for every visible vector layer,
// served from the view cache after the first bake. Returns true (the
// checkerboard beneath is painted by the caller).
bool paintOutlineContent(QPainter& p, const DocumentItem& doc,
                         const QTransform& docToView, double zoomEff,
                         const QRectF& docRect, const QRect& viewDirty);

}  // namespace pittore::ui
