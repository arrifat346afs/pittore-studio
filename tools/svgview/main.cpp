// svgview - SVG structure test app: canvas + layers panel.
//
// Demonstrates the FIX for merged groups. svgPartsImport (the real importer)
// merges structure through three passes in ui/svg_parts.cpp:
//   - flattenLeafRuns: runs of >=8 flattenable siblings become one image row
//   - sealBigGroups:   row-census rescue bakes every group into one image
//   - chunkLooseRuns:  loose-leaf runs become one image row
// Each of those rows is emitted with group=false, so the panel shows a single
// image where a group and its shapes belong.
//
// This app re-expands those rows back into structure using data the importer
// already retains (SvgPartLayer::flatArt member geometry, SvgPartLayer::
// shared subtrees) and the app's own rasterizers (rasterizeArtNode,
// sharedRowBake), so every group stays a group and every shape its own layer.
// Nothing in the production importer is modified.
//
// Usage: svgview file.svg [screenshot.png]

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPaintEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>
#include <QXmlStreamReader>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <stack>
#include <thread>
#include <vector>

#include "ui/persona/vector_raster.h"
#include "ui/svg_parts.h"

using pittore::ui::SvgImportResult;
using pittore::ui::SvgNode;
using pittore::ui::SvgPartLayer;
using pittore::ui::SvgResources;

namespace {

// ---------------------------------------------------------------------------
// Authored truth: quick tag census straight from the file
// ---------------------------------------------------------------------------

struct AuthoredStats {
    long groups = 0;
    long paths = 0;
    long elements = 0;
};

AuthoredStats censusAuthored(const QByteArray& file) {
    AuthoredStats st;
    QXmlStreamReader xml(file);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement) {
            ++st.elements;
            const QString tag = xml.name().toString();
            if (tag == QLatin1String("g")) ++st.groups;
            if (tag == QLatin1String("path")) ++st.paths;
        }
    }
    return st;
}

// Synthetic rows built by flattenLeafRuns/chunkLooseRuns are named
// "<first-name> +<n-1>" (see svg_parts.cpp). Real groups keep their id.
bool isSyntheticName(const QString& name) {
    const int i = name.lastIndexOf(QLatin1String(" +"));
    if (i < 0 || i + 2 >= name.size()) return false;
    for (int k = i + 2; k < name.size(); ++k)
        if (!name[k].isDigit()) return false;
    return i + 2 < name.size();
}

// ---------------------------------------------------------------------------
// The fix: re-expand merged/sealed image rows back into rows of structure
// ---------------------------------------------------------------------------

struct PlanRow {
    QString name;
    QString info;
    int depth = 0;
    bool group = false;
    int work = -1;  // raster work index (leaf rows built by the workers)
    QImage pixels;  // kept single-shape rows (import pixels, already exact)
    QPointF off;    // doc position of pixels(0,0)
    QSizeF draw;    // doc-space draw size (density-aware)
    double opacity = 1.0;
};

struct Work {
    const pittore::vector::ArtNode* art = nullptr;  // flat-row member
    const SvgNode* leaf = nullptr;                  // sealed-subtree leaf
    const SvgResources* res = nullptr;
    QPointF base;  // flat row: doc offset of the member's source space
    QImage img;
    QPointF off;
    QSizeF draw;
    bool ok = false;
    bool usedFallback = false;  // painted locally instead of sharedRowBake
};

struct Counters {
    int mergedRows = 0;   // flat rows re-expanded (flattenLeafRuns)
    int sealedRows = 0;   // authored groups re-expanded (sealBigGroups)
    int chunkRows = 0;    // loose runs re-expanded (chunkLooseRuns)
    int groupsRestored = 0;
    long shapesRestored = 0;
    long keptSingles = 0;
    int fallbackPaint = 0;  // leaves sharedRowBake refused (density-1 edge)
    int failed = 0;
};

// Fallback for leaves sharedRowBake refuses at density 1 (long edge > 1024
// doc px): a local re-implementation of the importer's per-leaf paint body
// (solid/gradient fill, stroke, embedded image, opacity, transform).
bool paintLeafLocal(const SvgNode& n, QImage* img, QPointF* off,
                    QSizeF* draw) {
    const QRectF b = n.transform.mapRect(n.path.boundingRect());
    if (b.isEmpty() && !(n.hasStroke && n.strokeWidth > 0)) return false;
    const double sc = std::max(
        {std::hypot(n.transform.m11(), n.transform.m12()),
         std::hypot(n.transform.m21(), n.transform.m22()), 1e-9});
    const double margin = n.hasStroke ? n.strokeWidth * sc / 2.0 + 1.0 : 1.0;
    const QRect ir = b.adjusted(-margin, -margin, margin, margin)
                         .toAlignedRect();
    if (ir.width() <= 0 || ir.height() <= 0 || ir.width() > 16384 ||
        ir.height() > 16384)
        return false;
    QImage out(ir.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.translate(-ir.x(), -ir.y());
        p.setOpacity(std::clamp(n.opacity, 0.0, 1.0));
        p.setTransform(n.transform, true);
        if (n.image && !n.image->isNull()) {
            p.setPen(Qt::NoPen);
            p.drawImage(n.path.boundingRect(), *n.image);
        } else if (n.hasFill) {
            p.setPen(Qt::NoPen);
            if (n.gradient) {
                const QRectF gb = n.path.boundingRect();
                if (n.gradient->radial) {
                    QRadialGradient g(
                        n.gradient->userSpace
                            ? QPointF(n.gradient->cx, n.gradient->cy)
                            : QPointF(gb.x() + gb.width() * n.gradient->cx,
                                      gb.y() + gb.height() * n.gradient->cy),
                        n.gradient->userSpace
                            ? n.gradient->r
                            : std::max(gb.width(), gb.height()) *
                                  n.gradient->r);
                    for (const auto& s : n.gradient->stops)
                        g.setColorAt(std::clamp(s.offset, 0.0, 1.0), s.color);
                    p.setBrush(QBrush(g));
                } else {
                    QLinearGradient g(
                        n.gradient->userSpace
                            ? QPointF(n.gradient->x1, n.gradient->y1)
                            : QPointF(gb.x() + gb.width() * n.gradient->x1,
                                      gb.y() + gb.height() * n.gradient->y1),
                        n.gradient->userSpace
                            ? QPointF(n.gradient->x2, n.gradient->y2)
                            : QPointF(gb.x() + gb.width() * n.gradient->x2,
                                      gb.y() + gb.height() * n.gradient->y2));
                    for (const auto& s : n.gradient->stops)
                        g.setColorAt(std::clamp(s.offset, 0.0, 1.0), s.color);
                    p.setBrush(QBrush(g));
                }
            } else {
                p.setBrush(QBrush(n.fill));
            }
            p.drawPath(n.path);
        }
        if (n.hasStroke) {
            QPen pen(n.stroke, std::max(0.01, n.strokeWidth));
            pen.setCosmetic(false);
            p.setPen(pen);
            if (!n.hasFill) p.setBrush(Qt::NoBrush);
            p.drawPath(n.path);
        }
    }
    *img = std::move(out);
    *off = QPointF(ir.x(), ir.y());
    *draw = QSizeF(ir.size());
    return true;
}

void rasterWork(Work& w) {
    if (w.art) {
        // Flat-row member: matrix maps node -> row source space (doc minus
        // the row origin), so the doc offset is base + trimmed source origin.
        QPointF trim;
        if (pittore::ui::rasterizeArtNode(*w.art, &w.img, &trim)) {
            w.off = QPointF(w.base.x() + trim.x(), w.base.y() + trim.y());
            w.draw = QSizeF(w.img.size());
            w.ok = true;
        }
        return;
    }
    if (!w.leaf) return;
    // Sealed-subtree leaf: wrap in a synthetic group and let the app's own
    // shared-row baker paint it with the exact import painter (density 2,
    // so big-leaf refusal falls to the local painter below).
    QImage img;
    QPointF origin;
    double k = 1.0;
    QRectF box;
    if (w.res) {
        SvgNode wrap;
        wrap.group = true;
        wrap.children.push_back(*w.leaf);
        if (pittore::ui::sharedRowBake(wrap, *w.res, QTransform(), 2.0,
                                       QRectF(), &img, &origin, &k, &box) &&
            !img.isNull()) {
            w.img = std::move(img);
            w.off = origin;
            w.draw = box.size();
            w.ok = true;
            return;
        }
    }
    w.usedFallback = true;
    w.ok = paintLeafLocal(*w.leaf, &w.img, &w.off, &w.draw);
}

// Walk a retained subtree top-first (last painted first), emitting group
// headers for real groups and raster work for leaves. Synthetic merged
// groups inside the subtree are transparent: their children lift to the
// same depth - the fix never shows a merged image as a layer.
void emitSubtree(const SvgNode& n, int depth, bool header,
                 std::vector<PlanRow>& plan, std::vector<Work>& works,
                 Counters& ct) {
    int childDepth = depth;
    if (header) {
        PlanRow r;
        r.name = n.name;
        r.info = QStringLiteral("group");
        r.depth = depth;
        r.group = true;
        plan.push_back(std::move(r));
        ++ct.groupsRestored;
        childDepth = depth + 1;
    }
    for (auto it = n.children.crbegin(); it != n.children.crend(); ++it) {
        const SvgNode& c = *it;
        if (c.group) {
            emitSubtree(c, childDepth, !isSyntheticName(c.name), plan, works,
                        ct);
            continue;
        }
        Work w;
        w.leaf = &c;
        w.res = nullptr;  // filled by caller for top-level sealed rows
        PlanRow r;
        r.name = c.name.isEmpty() ? QStringLiteral("shape") : c.name;
        r.info = QStringLiteral("shape - restored from sealed image");
        r.depth = childDepth;
        r.work = static_cast<int>(works.size());
        works.push_back(w);
        plan.push_back(std::move(r));
        ++ct.shapesRestored;
    }
}

void explode(const SvgImportResult& in, std::vector<PlanRow>& plan,
             std::vector<Work>& works, Counters& ct) {
    for (const SvgPartLayer& p : in.layers) {
        if (p.group) {
            PlanRow r;
            r.name = p.name.isEmpty() ? QStringLiteral("Group") : p.name;
            r.info = QStringLiteral("group");
            r.depth = p.depth;
            r.group = true;
            plan.push_back(std::move(r));
            ++ct.groupsRestored;
            continue;
        }
        if (!p.flatArt.empty()) {
            // flattenLeafRuns row: members are ArtNodes in row source space.
            ++ct.mergedRows;
            const int total = static_cast<int>(p.flatArt.size());
            for (int i = total - 1; i >= 0; --i) {
                const auto& member = p.flatArt[static_cast<size_t>(i)];
                Work w;
                w.art = member.get();
                w.base = p.offset;
                PlanRow r;
                r.name = member->name.empty()
                             ? QStringLiteral("shape")
                             : QString::fromStdString(member->name);
                r.info = QStringLiteral(
                             "shape %1/%2 - restored from merged image row")
                             .arg(i + 1)
                             .arg(total);
                r.depth = p.depth;
                r.work = static_cast<int>(works.size());
                works.push_back(w);
                plan.push_back(std::move(r));
                ++ct.shapesRestored;
            }
            continue;
        }
        if (p.shared) {
            const bool synth = isSyntheticName(p.name);
            if (synth)
                ++ct.chunkRows;
            else
                ++ct.sealedRows;
            const SvgResources* res = p.sharedRes.get();
            // Seed the subtree's leaves with this row's resources.
            const size_t before = works.size();
            emitSubtree(*p.shared, p.depth, !synth, plan, works, ct);
            for (size_t i = before; i < works.size(); ++i)
                works[i].res = res;
            continue;
        }
        // Kept single-shape row: exact import pixels, nothing to restore.
        PlanRow r;
        r.name = p.name.isEmpty() ? QStringLiteral("shape") : p.name;
        r.info = QStringLiteral("shape");
        r.depth = p.depth;
        r.pixels = p.pixels;
        r.off = p.offset;
        r.draw = QSizeF(p.pixels.size());
        r.opacity = p.opacity;
        plan.push_back(std::move(r));
        ++ct.keptSingles;
    }
}

// ---------------------------------------------------------------------------
// Canvas: document image with wheel zoom, drag pan, selection highlight
// ---------------------------------------------------------------------------

class CanvasView final : public QWidget {
public:
    void setDocument(QImage doc) {
        m_doc = std::move(doc);
        m_fitted = false;
        update();
    }
    void setHighlight(const QRectF& docRect) {
        m_highlight = docRect;
        update();
    }
    void fit() {
        if (m_doc.isNull() || width() <= 0 || height() <= 0) return;
        const double sx = width() / double(m_doc.width());
        const double sy = height() / double(m_doc.height());
        m_scale = std::min(sx, sy) * 0.97;
        m_off = QPointF((width() - m_doc.width() * m_scale) / 2.0,
                        (height() - m_doc.height() * m_scale) / 2.0);
        m_fitted = true;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(58, 58, 62));
        if (m_doc.isNull()) return;
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.translate(m_off);
        p.scale(m_scale, m_scale);
        p.drawImage(0, 0, m_doc);
        if (!m_highlight.isEmpty()) {
            QPen pen(QColor(255, 200, 0), 2.0 / m_scale);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(m_highlight.adjusted(-1, -1, 1, 1));
        }
        // Viewport box in doc space (what "missing here?" looks like).
        const QRectF vis(-m_off.x() / m_scale, -m_off.y() / m_scale,
                         width() / m_scale, height() / m_scale);
        QPen vp(QColor(120, 160, 255, 120), 1.0 / m_scale);
        vp.setStyle(Qt::DashLine);
        p.setPen(vp);
        p.drawRect(vis);
    }
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        if (!m_fitted) fit();
    }
    void wheelEvent(QWheelEvent* e) override {
        if (m_doc.isNull()) return;
        const QPointF anchor = QPointF(e->position()) - m_off;
        const QPointF docPt = anchor / m_scale;
        const double factor = std::pow(1.0015, e->angleDelta().y());
        m_scale = std::clamp(m_scale * factor, 0.02, 64.0);
        m_off = QPointF(e->position()) - docPt * m_scale;
        update();
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_last = e->position();
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_dragging) {
            m_off += e->position() - m_last;
            m_last = e->position();
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragging = false;
            unsetCursor();
        }
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_F || e->key() == Qt::Key_0) fit();
        else QWidget::keyPressEvent(e);
    }

private:
    QImage m_doc;
    double m_scale = 1.0;
    QPointF m_off;
    QPointF m_last;
    bool m_dragging = false;
    bool m_fitted = false;
    QRectF m_highlight;
};

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: svgview file.svg [screenshot.png]\n");
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    const QByteArray data = f.readAll();
    f.close();

    const AuthoredStats authored = censusAuthored(data);

    // Shell first so the user sees something while the import runs.
    auto* canvas = new CanvasView();
    auto* tree = new QTreeWidget();
    tree->setHeaderLabels({QStringLiteral("Layer"), QStringLiteral("Info")});
    tree->setUniformRowHeights(true);
    auto* status = new QStatusBar();
    status->showMessage(QStringLiteral("importing..."));
    auto* split = new QSplitter();
    split->addWidget(canvas);
    split->addWidget(tree);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    split->setSizes({1200, 420});
    QWidget win;
    auto* vl = new QVBoxLayout(&win);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    vl->addWidget(split, 1);
    vl->addWidget(status);
    win.setWindowTitle(QStringLiteral("svgview - %1").arg(
        QFileInfo(path).fileName()));
    win.resize(1680, 1000);
    win.show();
    app.processEvents();

    // Real importer (unchanged) - this is what produces merged rows.
    SvgImportResult import;
    int dpi = 96;
    QString importError;
    const bool ok = svgPartsImport(data, &import, &dpi, &importError);
    if (!ok || import.docSize.isEmpty()) {
        status->showMessage(QStringLiteral("import failed: ") + importError);
        std::printf("SVGVIEW import failed: %s\n",
                    importError.toUtf8().constData());
        if (argc >= 3) {
            app.processEvents();
            win.grab().save(QString::fromLocal8Bit(argv[2]));
        }
        return app.exec();
    }

    // The fix: re-expand every merged/sealed image row into structure.
    std::vector<PlanRow> plan;
    std::vector<Work> works;
    Counters ct;
    explode(import, plan, works, ct);

    // Raster the restored leaf rows in parallel (app rasterizers).
    QElapsedTimer t;
    t.start();
    if (!works.empty()) {
        std::atomic<size_t> next{0};
        const unsigned hw = std::thread::hardware_concurrency();
        const int nthreads = (int)std::min<unsigned>(hw < 2 ? 2u : hw, 16);
        std::vector<std::thread> pool;
        pool.reserve((size_t)nthreads);
        for (int i = 0; i < nthreads; ++i) {
            pool.emplace_back([&] {
                while (true) {
                    const size_t j = next.fetch_add(1);
                    if (j >= works.size()) return;
                    rasterWork(works[j]);
                }
            });
        }
        for (auto& th : pool) th.join();
    }
    long baked = 0;
    for (const Work& w : works) {
        if (!w.ok) {
            if (w.art) ++ct.failed;
            else ++ct.fallbackPaint;
        } else if (w.usedFallback) {
            ++ct.fallbackPaint;
        } else {
            ++baked;
        }
    }

    // Composite: paint bottom (last row) first, panel order preserved.
    QImage doc(import.docSize, QImage::Format_ARGB32_Premultiplied);
    doc.fill(Qt::transparent);
    {
        QPainter p(&doc);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (auto it = plan.rbegin(); it != plan.rend(); ++it) {
            PlanRow& r = *it;
            if (r.work >= 0) {
                Work& w = works[(size_t)r.work];
                if (!w.ok) continue;
                r.pixels = std::move(w.img);
                r.off = w.off;
                r.draw = w.draw;
                r.opacity = 1.0;
            }
            if (r.pixels.isNull()) continue;
            p.setOpacity(std::clamp(r.opacity, 0.0, 1.0));
            p.drawImage(QRectF(r.off, r.draw), r.pixels);
        }
    }
    // Free the per-row images; the composite and the tree need no pixels.
    for (PlanRow& r : plan) r.pixels = QImage();

    // Layers panel: structure tree in panel order (top-first).
    std::vector<QTreeWidgetItem*> stk;
    std::vector<int> depths;
    std::vector<QRectF> rects(plan.size());
    for (size_t i = 0; i < plan.size(); ++i) {
        const PlanRow& r = plan[i];
        rects[i] = QRectF(r.off, r.draw);
        while (!depths.empty() && depths.back() >= r.depth) {
            depths.pop_back();
            stk.pop_back();
        }
        QTreeWidgetItem* item;
        if (stk.empty()) {
            item = new QTreeWidgetItem();
            tree->addTopLevelItem(item);
        } else {
            item = new QTreeWidgetItem(stk.back());
        }
        item->setText(0, r.name);
        item->setText(1, r.info);
        item->setData(0, Qt::UserRole, (qulonglong)i);
        if (r.group) {
            stk.push_back(item);
            depths.push_back(r.depth);
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
        }
    }
    tree->expandToDepth(1);

    canvas->setDocument(std::move(doc));
    QObject::connect(tree, &QTreeWidget::itemSelectionChanged, [&] {
        const auto items = tree->selectedItems();
        if (items.isEmpty()) {
            canvas->setHighlight(QRectF());
            return;
        }
        const size_t i = (size_t)items.front()->data(0, Qt::UserRole).toULongLong();
        if (i < rects.size()) {
            canvas->setHighlight(rects[i]);
            status->showMessage(
                QStringLiteral("%1  [%2]")
                    .arg(items.front()->text(0))
                    .arg(items.front()->text(1)));
        }
        canvas->update();
    });

    int panelGroups = 0;
    long panelShapes = 0;
    for (const PlanRow& r : plan) {
        if (r.group) ++panelGroups;
        else ++panelShapes;
    }
    const QString summary = QStringLiteral(
        "authored: %1 groups / %2 paths  |  panel: %3 rows = %4 groups + "
        "%5 shapes  |  restored: %6 merged + %7 sealed + %8 chunked rows, "
        "%9 groups, %10 shapes  |  raster fails %11")
                                .arg(authored.groups)
                                .arg(authored.paths)
                                .arg(plan.size())
                                .arg(panelGroups)
                                .arg(panelShapes)
                                .arg(ct.mergedRows)
                                .arg(ct.sealedRows)
                                .arg(ct.chunkRows)
                                .arg(ct.groupsRestored)
                                .arg(ct.shapesRestored)
                                .arg(ct.failed + ct.fallbackPaint);
    status->showMessage(summary);

    std::printf("SVGVIEW file=%s bytes=%lld\n", argv[1], (long long)data.size());
    std::printf("SVGVIEW authored: groups=%ld paths=%ld elements=%ld\n",
                authored.groups, authored.paths, authored.elements);
    std::printf("SVGVIEW import(buggy): rows=%d flatRows=%d flatArtRows=%d "
                "sharedRows=%d groups=%d sealed=%d chunked=%d\n",
                (int)import.layers.size(), import.diag.flatRows,
                import.diag.layersFlatArt, import.diag.layersShared,
                import.diag.layersGroup, import.diag.sealedGroups,
                import.diag.chunkedRuns);
    std::printf("SVGVIEW fixed: rows=%zu groups=%d shapes=%ld mergedRows=%d "
                "sealedRows=%d chunkRows=%d keptSingles=%ld baked=%ld "
                "fallbackPaint=%d failed=%d ms=%lld\n",
                plan.size(), panelGroups, panelShapes, ct.mergedRows,
                ct.sealedRows, ct.chunkRows, ct.keptSingles, baked,
                ct.fallbackPaint, ct.failed, (long long)t.elapsed());
    std::fflush(stdout);

    if (argc >= 3) {
        app.processEvents();
        app.processEvents();
        const bool saved =
            win.grab().save(QString::fromLocal8Bit(argv[2]));
        std::printf("SVGVIEW shot=%s saved=%d\n", argv[2], (int)saved);
        std::fflush(stdout);
        return saved ? 0 : 1;
    }
    return app.exec();
}
