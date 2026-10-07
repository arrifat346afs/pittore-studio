// Vector panels: align/distribute, transform, XML editor, object
// properties, SVG filter editor, symbols/clones, document properties, trace
// bitmap, extensions, pages, markers. Each panel is a thin Qt view over the
// toolkit-free engine in src/engine/vector (transform_ops, snap/grids,
// svg_dom, filter_fe, clone/marker/pattern, trace, palette, lpe, svg_exchange)
// so the layout stays testable without a canvas.
#include "ui/panels/registry/panel_creators.h"

#include "ui/panels.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

#include "engine/vector/filter_fe.h"
#include "engine/vector/svg_exchange.h"
#include "engine/vector/lpe/lpe.h"
#include "engine/vector/marker.h"
#include "engine/vector/path_ops.h"
#include "engine/vector/pattern.h"
#include "engine/vector/trace.h"
#include "engine/vector/transform_ops.h"
#include "ui/app_state.h"
#include "ui/export_dialog.h"
#include "ui/panels/shared/panel_helpers.h"
#include "ui/persona/vector_edit.h"
#include "ui/svg_bridge.h"
#include "ui/svg_parts.h"

namespace pittore::ui {
namespace {

// User script-extension directory (created on first use).
QString extensionDirectory() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
        QStringLiteral("/extensions");
    QDir().mkpath(dir);
    return dir;
}

}  // namespace

QWidget* createAlignPanel(AppState* state, QWidget* parent) {
    // Align/distribute/arrange the selected layers via transform_ops; one
    // undo step per action through moveLayersAt.
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* row1 = new QListWidget(w);
    row1->addItems({"Align left", "Align H-center", "Align right", "Align top",
                    "Align V-center", "Align bottom"});
    auto* row2 = new QListWidget(w);
    row2->addItems({"Distribute H-gap", "Distribute V-gap", "Distribute H-center",
                    "Distribute V-center", "Arrange grid", "Arrange circle",
                    "Arrange polar", "Arrange honeycomb"});
    col->addWidget(new QLabel(QObject::tr("Align"), w));
    col->addWidget(row1, 1);
    col->addWidget(new QLabel(QObject::tr("Distribute / Arrange"), w));
    col->addWidget(row2, 1);
    auto runAlign = [state](const QString& what) {
        DocumentItem* d = state->activeDocument();
        if (!d) return;
        const QVector<int> idx = state->selectedLayerIndices();
        if (idx.size() < 2) {
            state->setStatusHint(QObject::tr("Select two or more layers."));
            return;
        }
        std::vector<vector::Bbox> boxes;
        for (int i : idx) {
            if (i < 0 || i >= d->layers.size()) continue;
            const QRectF b = layerBounds(*d, d->layers[i]);
            boxes.push_back({b.x(), b.y(), b.x() + b.width(), b.y() + b.height()});
        }
        if (boxes.size() < 2) {
            state->setStatusHint(QObject::tr("Select two or more layers."));
            return;
        }
        auto edge = vector::AlignEdge::Left;
        double to = 0;
        auto span = [&](bool horiz, bool lo) {
            double v = lo ? 1e100 : -1e100;
            for (auto& b : boxes) {
                double e = horiz ? (lo ? b.x0 : b.x1) : (lo ? b.y0 : b.y1);
                v = lo ? std::min(v, e) : std::max(v, e);
            }
            return v;
        };
        if (what == "Align left") {
            edge = vector::AlignEdge::Left;
            to = span(true, true);
        } else if (what == "Align H-center") {
            edge = vector::AlignEdge::HCenter;
            to = 0;
            for (auto& b : boxes) to += b.cx();
            to /= boxes.size();
        } else if (what == "Align right") {
            edge = vector::AlignEdge::Right;
            to = span(true, false);
        } else if (what == "Align top") {
            edge = vector::AlignEdge::Top;
            to = span(false, true);
        } else if (what == "Align V-center") {
            edge = vector::AlignEdge::VCenter;
            to = 0;
            for (auto& b : boxes) to += b.cy();
            to /= boxes.size();
        } else {
            edge = vector::AlignEdge::Bottom;
            to = span(false, false);
        }
        const auto moves = vector::alignBoxes(boxes, edge, to);
        QVector<int> ids;
        for (int i : idx)
            if (i >= 0 && i < d->layers.size()) ids.push_back(i);
        for (size_t k = 0; k < moves.size() && k < (size_t)ids.size(); k++) {
            const auto [dx, dy] = moves[k];
            if (dx != 0 || dy != 0)
                state->moveLayersAt({ids[(int)k]}, QPointF(dx, dy));
        }
        state->setStatusHint(QObject::tr("Aligned."));
    };
    auto runDistribute = [state](const QString& what) {
        DocumentItem* d = state->activeDocument();
        if (!d) return;
        const QVector<int> idx = state->selectedLayerIndices();
        if (idx.size() < 3) {
            state->setStatusHint(QObject::tr("Select three or more layers."));
            return;
        }
        std::vector<vector::Bbox> boxes;
        QVector<int> ids;
        for (int i : idx) {
            if (i < 0 || i >= d->layers.size()) continue;
            const QRectF b = layerBounds(*d, d->layers[i]);
            boxes.push_back({b.x(), b.y(), b.x() + b.width(), b.y() + b.height()});
            ids.push_back(i);
        }
        if (what.startsWith("Arrange")) {
            vector::ArrangeSpec spec;
            if (what.endsWith("circle") || what.endsWith("polar"))
                spec.kind = vector::ArrangeKind::Circle;
            else if (what.endsWith("honeycomb"))
                spec.kind = vector::ArrangeKind::Honeycomb;
            else
                spec.kind = vector::ArrangeKind::Grid;
            spec.cols = (int)std::ceil(std::sqrt(ids.size()));
            double x0 = 1e100, y0 = 1e100;
            for (auto& b : boxes) {
                x0 = std::min(x0, b.x0);
                y0 = std::min(y0, b.y0);
            }
            const auto cells = vector::arrangePositions(ids.size(), spec);
            // Anchor slot 0 at the first box; move the rest by delta.
            for (int k = 1; k < ids.size() && k < (int)cells.size(); k++) {
                double dx = (x0 + cells[(size_t)k].first) -
                            (x0 + cells[0].first) - (boxes[(size_t)k].x0 - boxes[0].x0);
                double dy = (y0 + cells[(size_t)k].second) -
                            (y0 + cells[0].second) - (boxes[(size_t)k].y0 - boxes[0].y0);
                state->moveLayersAt({ids[k]}, QPointF(dx, dy));
            }
            state->setStatusHint(QObject::tr("Arranged."));
            return;
        }
        vector::DistributeKind kind = vector::DistributeKind::HGap;
        if (what.endsWith("V-gap")) kind = vector::DistributeKind::VGap;
        else if (what.endsWith("H-center")) kind = vector::DistributeKind::HCenter;
        else if (what.endsWith("V-center")) kind = vector::DistributeKind::VCenter;
        const auto moves = vector::distributeBoxes(boxes, kind);
        for (size_t k = 0; k < moves.size() && k < (size_t)ids.size(); k++) {
            const auto [dx, dy] = moves[k];
            if (dx != 0 || dy != 0) state->moveLayersAt({ids[k]}, QPointF(dx, dy));
        }
        state->setStatusHint(QObject::tr("Distributed."));
    };
    QObject::connect(row1, &QListWidget::itemClicked, row1,
                     [runAlign](QListWidgetItem* it) { runAlign(it->text()); });
    QObject::connect(row2, &QListWidget::itemClicked, row2,
                     [runDistribute](QListWidgetItem* it) { runDistribute(it->text()); });
    col->addWidget(makePanelFooter(
        state, {{QStringLiteral("align"), QObject::tr("Apply selected")}}, w,
        [runAlign, runDistribute, row1, row2](const QString&) {
            if (row1->currentItem()) {
                runAlign(row1->currentItem()->text());
                return;
            }
            if (row2->currentItem()) runDistribute(row2->currentItem()->text());
        }));
    return w;
}

QWidget* createTransformPanel(AppState* state, QWidget* parent) {
    // Numeric transform over the selected layers: translate + uniform scale
    // about the selection center for every layer; rotation bakes into vector
    // art (the layer model has no rotation field — pixel rows refuse loudly).
    auto* w = new QWidget(parent);
    auto* top = new QVBoxLayout(w);
    top->setContentsMargins(0, 4, 0, 0);
    auto* form = new QFormLayout();
    auto* dx = new QDoubleSpinBox(w);
    dx->setRange(-9999, 9999);
    auto* dy = new QDoubleSpinBox(w);
    dy->setRange(-9999, 9999);
    auto* rot = new QDoubleSpinBox(w);
    rot->setRange(-360, 360);
    auto* sc = new QDoubleSpinBox(w);
    sc->setRange(0.01, 99);
    sc->setSingleStep(0.05);
    sc->setValue(1.0);
    form->addRow(QObject::tr("DX"), dx);
    form->addRow(QObject::tr("DY"), dy);
    form->addRow(QObject::tr("Rotate°"), rot);
    form->addRow(QObject::tr("Scale"), sc);
    top->addLayout(form);
    top->addStretch(1);
    top->addWidget(makePanelFooter(
        state, {{QStringLiteral("xform-apply"), QObject::tr("Apply")}}, w,
        [state, dx, dy, rot, sc](const QString&) {
            DocumentItem* d = state->activeDocument();
            if (!d) return;
            const QVector<int> idx = state->selectedLayerIndices();
            if (idx.isEmpty()) {
                state->setStatusHint(QObject::tr("Select one or more layers."));
                return;
            }
            const double mdx = dx->value(), mdy = dy->value();
            const double s = sc->value(), r = rot->value();
            if (mdx != 0 || mdy != 0) state->moveLayersAt(idx, QPointF(mdx, mdy));
            if (s != 1.0) {
                // Scale about the selection center (offsets shift with it).
                double cx = 0, cy = 0;
                int n = 0;
                for (int i : idx) {
                    if (i < 0 || i >= d->layers.size()) continue;
                    const QRectF b = layerBounds(*d, d->layers[i]);
                    cx += b.center().x();
                    cy += b.center().y();
                    n++;
                }
                if (n > 0) {
                    cx /= n;
                    cy /= n;
                    QVector<QPointF> offs;
                    QVector<double> sxs, sys;
                    QVector<int> ids;
                    for (int i : idx) {
                        if (i < 0 || i >= d->layers.size()) continue;
                        const LayerItem& l = d->layers[i];
                        offs.push_back(QPointF(cx + (l.offset.x() - cx) * s,
                                              cy + (l.offset.y() - cy) * s));
                        sxs.push_back(l.scaleX * s);
                        sys.push_back(l.scaleY * s);
                        ids.push_back(i);
                    }
                    state->setLayerPlacements(ids, offs, sxs, sys);
                }
            }
            if (r != 0) {
                const double rad = r * 3.14159265358979 / 180.0;
                const double cr = std::cos(rad), sr = std::sin(rad);
                bool skipped = false;
                for (int i : idx) {
                    if (i < 0 || i >= d->layers.size()) continue;
                    const LayerItem& l = d->layers[i];
                    if (!l.art || l.art->isEmpty()) {
                        skipped = true;
                        continue;
                    }
                    pittore::vector::ArtNode work = *l.art;
                    double mx = 0, my = 0, cn = 0;
                    for (const auto& sg : work.segments) {
                        if (sg.kind == pittore::vector::Segment::Kind::Close)
                            continue;
                        mx += sg.x;
                        my += sg.y;
                        cn++;
                    }
                    if (cn == 0) continue;
                    mx /= cn;
                    my /= cn;
                    for (auto& sg : work.segments) {
                        if (sg.kind == pittore::vector::Segment::Kind::Close)
                            continue;
                        auto rotPt = [&](float& x, float& y) {
                            double lx = x - mx, ly = y - my;
                            x = (float)(mx + lx * cr - ly * sr);
                            y = (float)(my + lx * sr + ly * cr);
                        };
                        rotPt(sg.x, sg.y);
                        if (sg.kind == pittore::vector::Segment::Kind::CubicTo) {
                            rotPt(sg.c1x, sg.c1y);
                            rotPt(sg.c2x, sg.c2y);
                        }
                    }
                    state->applyVectorNode(i, work, QObject::tr("Rotate"));
                }
                if (skipped)
                    state->setStatusHint(
                        QObject::tr("Rotated vector art (pixel layers have no rotation)."));
                else
                    state->setStatusHint(QObject::tr("Transformed."));
            } else {
                state->setStatusHint(QObject::tr("Transformed."));
            }
        }));
    return w;
}

QWidget* createXmlPanel(AppState* state, QWidget* parent) {
    // Live XML tree over the retained vector layers: select a node, edit
    // `attr=value` lines, Apply rewrites the ArtNode (one undo step).
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* tree = new QTreeWidget(w);
    tree->setHeaderLabels({QObject::tr("Node"), QObject::tr("Paint")});
    auto* edit = new QPlainTextEdit(w);
    edit->setPlaceholderText(QObject::tr("attr=value per line…"));
    edit->setMaximumHeight(110);
    auto rebuild = [state, tree] {
        tree->clear();
        const DocumentItem* d = state->activeDocument();
        if (!d) return;
        for (int i = 0; i < d->layers.size(); i++) {
            const LayerItem& l = d->layers[i];
            if (!l.art || l.art->isEmpty()) continue;
            auto* item = new QTreeWidgetItem(
                tree, {l.name, artNodeToXmlLine(*l.art).left(120)});
            item->setData(0, Qt::UserRole, i);
        }
    };
    rebuild();
    QObject::connect(tree, &QTreeWidget::itemSelectionChanged, tree,
                     [state, tree, edit] {
                         const auto sel = tree->selectedItems();
                         if (sel.isEmpty()) return;
                         const int idx = sel.first()->data(0, Qt::UserRole).toInt();
                         const DocumentItem* d = state->activeDocument();
                         if (!d || idx < 0 || idx >= d->layers.size()) return;
                         const LayerItem& l = d->layers[idx];
                         if (!l.art) return;
                         const auto& p = l.art->paint;
                         QStringList lines;
                         lines << QString("id=%1").arg(QString::fromStdString(l.art->name));
                         auto hex = [](const std::uint8_t c[4]) {
                             char b[16];
                             snprintf(b, sizeof(b), "#%02x%02x%02x", c[0], c[1], c[2]);
                             return QString(b);
                         };
                         lines << QString("fill=%1").arg(
                             p.patternId.empty() ? (p.hasFill ? hex(p.fill) : "none")
                                                 : "url(#" +
                                                       QString::fromStdString(p.patternId) +
                                                       ")");
                         lines << QString("stroke=%1").arg(p.hasStroke ? hex(p.stroke)
                                                                       : "none");
                         lines << QString("stroke-width=%1").arg(p.strokeWidth);
                         lines << QString("opacity=%1").arg(l.art->opacity, 0, 'f', 3);
                         if (!p.markerStart.empty())
                             lines << QString("marker-start=url(#%1)")
                                          .arg(QString::fromStdString(p.markerStart));
                         if (!p.markerMid.empty())
                             lines << QString("marker-mid=url(#%1)")
                                          .arg(QString::fromStdString(p.markerMid));
                         if (!p.markerEnd.empty())
                             lines << QString("marker-end=url(#%1)")
                                          .arg(QString::fromStdString(p.markerEnd));
                         if (!p.clipId.empty())
                             lines << QString("clip-path=url(#%1)")
                                          .arg(QString::fromStdString(p.clipId));
                         if (!p.maskId.empty())
                             lines << QString("mask=url(#%1)")
                                          .arg(QString::fromStdString(p.maskId));
                         if (p.hasFilter)
                             lines << QString("filter=url(#%1)")
                                          .arg(QString::fromStdString(p.filter.id));
                         edit->setPlainText(lines.join("\n"));
                     });
    col->addWidget(tree, 1);
    col->addWidget(edit);
    col->addWidget(makePanelFooter(
        state,
        {{QStringLiteral("xml-refresh"), QObject::tr("Refresh")},
         {QStringLiteral("xml"), QObject::tr("Apply")}},
        w, [state, tree, edit, rebuild](const QString& id) {
            if (id == QStringLiteral("xml-refresh")) {
                rebuild();
                return;
            }
            const auto sel = tree->selectedItems();
            if (sel.isEmpty()) return;
            const int idx = sel.first()->data(0, Qt::UserRole).toInt();
            DocumentItem* d = state->activeDocument();
            if (!d || idx < 0 || idx >= d->layers.size()) return;
            const LayerItem& l = d->layers[idx];
            if (!l.art) return;
            pittore::vector::ArtNode work = *l.art;
            for (const QString& line : edit->toPlainText().split("\n")) {
                const int eq = line.indexOf('=');
                if (eq <= 0) continue;
                QString err;
                if (!applyXmlEditToArt(work, line.left(eq).trimmed(),
                                       line.mid(eq + 1).trimmed(), &err)) {
                    state->setStatusHint(err);
                    return;
                }
            }
            if (state->applyVectorNode(idx, work, QObject::tr("XML Edit"))) rebuild();
        }));
    return w;
}

QWidget* createObjectPropsPanel(AppState* state, QWidget* parent) {
    // ID/label rename (Layers-panel undo contract) + live transform readout
    // for the editable layer.
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* form = new QFormLayout();
    auto* id = new QComboBox(w);
    id->setEditable(true);
    auto* label = new QComboBox(w);
    label->setEditable(true);
    auto* xform = new QLabel(w);
    xform->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QObject::tr("ID"), id);
    form->addRow(QObject::tr("Label"), label);
    form->addRow(QObject::tr("Transform"), xform);
    col->addLayout(form);
    col->addStretch(1);
    auto refresh = [state, id, label, xform] {
        const DocumentItem* d = state->activeDocument();
        const int index = d ? vectorEditableLayer(state) : -1;
        if (!d || index < 0 || index >= d->layers.size()) {
            id->clear();
            label->clear();
            xform->setText(QObject::tr("—"));
            return;
        }
        const LayerItem& l = d->layers[index];
        id->clear();
        id->addItem(l.name);
        label->clear();
        label->addItem(l.name);
        if (l.art) {
            const double* m = l.art->matrix;
            xform->setText(QString("matrix(%1 %2 %3 %4 %5 %6)")
                               .arg(m[0], 0, 'f', 3)
                               .arg(m[1], 0, 'f', 3)
                               .arg(m[2], 0, 'f', 3)
                               .arg(m[3], 0, 'f', 3)
                               .arg(m[4], 0, 'f', 1)
                               .arg(m[5], 0, 'f', 1));
        } else {
            xform->setText(QObject::tr("offset(%1, %2) scale(%3, %4)")
                               .arg(l.offset.x(), 0, 'f', 1)
                               .arg(l.offset.y(), 0, 'f', 1)
                               .arg(l.scaleX, 0, 'f', 3)
                               .arg(l.scaleY, 0, 'f', 3));
        }
    };
    refresh();
    col->addWidget(makePanelFooter(
        state,
        {{QStringLiteral("obj-refresh"), QObject::tr("Refresh")},
         {QStringLiteral("obj-rename"), QObject::tr("Rename")}},
        w, [state, id, refresh](const QString& which) {
            if (which == QStringLiteral("obj-refresh")) {
                refresh();
                return;
            }
            DocumentItem* d = state->activeDocument();
            const int index = d ? vectorEditableLayer(state) : -1;
            if (!d || index < 0 || index >= d->layers.size()) return;
            const QString next = id->currentText().trimmed();
            if (next.isEmpty() || next == d->layers[index].name) return;
            state->beginUndoStep();
            d->layers[index].name = next;
            state->commitUndoStep(QObject::tr("Rename Layer"),
                                  QStringLiteral("rename"));
            emit state->documentModified(d);
            emit state->layersChanged();
            refresh();
        }));
    return w;
}

QWidget* createFilterEditorPanel(AppState* state, QWidget* parent) {
    // Gallery pick → retained FilterGraph. Applies to the editable vector
    // layer (re-emitted on SVG export) or raster-runs the active pixel layer
    // whole-layer (one undo step, same contract as Invert).
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* list = new QListWidget(w);
    const auto& catalog = vector::svgFilterCatalog();
    for (auto& e : catalog)
        list->addItem(QString::fromStdString(e.category + ": " + e.label));
    col->addWidget(list, 1);
    auto recipe = [](const std::string& id) {
        using P = vector::FePrimitive;
        vector::FilterGraph g;
        g.id = id;
        auto prim = [&](const std::string& t,
                        std::map<std::string, std::string> attrs = {},
                        const std::string& result = "",
                        const std::string& in = "") {
            P p;
            p.type = t;
            p.attrs = std::move(attrs);
            p.result = result;
            p.in = in;
            g.prims.push_back(std::move(p));
        };
        if (id == "blur-subtle") prim("feGaussianBlur", {{"stdDeviation", "1.5"}});
        else if (id == "blur-strong") prim("feGaussianBlur", {{"stdDeviation", "6"}});
        else if (id == "shadow-drop")
            prim("feDropShadow", {{"dx", "4"}, {"dy", "4"}, {"stdDeviation", "4"},
                                  {"flood-opacity", "0.6"}});
        else if (id == "shadow-inner")
            prim("feMorphology", {{"operator", "erode"}, {"radius", "2"}}, "e");
        else if (id == "glow-neon") {
            prim("feGaussianBlur", {{"stdDeviation", "3"}}, "b");
            prim("feMerge", {}, "", "");
            g.prims.back().attrs["nodes"] = "b,SourceGraphic";
        } else if (id == "light-emboss")
            prim("feDiffuseLighting", {{"surfaceScale", "2"}});
        else if (id == "grain-film") {
            prim("feTurbulence",
                 {{"baseFrequency", "0.9"}, {"numOctaves", "2"}}, "n");
            prim("feColorMatrix", {{"type", "saturate"}, {"values", "0"}}, "", "n");
        } else if (id == "grain-turbulence")
            prim("feTurbulence", {{"baseFrequency", "0.05"}, {"numOctaves", "4"}});
        else if (id == "paint-oil") {
            prim("feMorphology", {{"operator", "dilate"}, {"radius", "2"}}, "d");
            prim("feGaussianBlur", {{"stdDeviation", "1"}}, "", "d");
        } else if (id == "paint-watercolor") {
            prim("feTurbulence",
                 {{"baseFrequency", "0.08"}, {"numOctaves", "2"}}, "n");
            prim("feDisplacementMap", {{"scale", "8"}, {"in2", "n"}});
        } else if (id == "edge-detect")
            prim("feConvolveMatrix", {{"order", "3"},
                                      {"kernelMatrix", "-1 -1 -1 -1 8 -1 -1 -1 -1"}});
        else if (id == "edge-chisel")
            prim("feConvolveMatrix", {{"order", "3"},
                                      {"kernelMatrix", "-2 -1 0 -1 1 1 0 1 2"}});
        else if (id == "morph-erode")
            prim("feMorphology", {{"operator", "erode"}, {"radius", "2"}});
        else if (id == "morph-dilate")
            prim("feMorphology", {{"operator", "dilate"}, {"radius", "2"}});
        else if (id == "displace-wave") {
            prim("feTurbulence",
                 {{"baseFrequency", "0.05"}, {"numOctaves", "2"}}, "n");
            prim("feDisplacementMap", {{"scale", "12"}, {"in2", "n"}});
        } else if (id == "displace-ripple") {
            prim("feTurbulence",
                 {{"baseFrequency", "0.2"}, {"numOctaves", "3"}}, "n");
            prim("feDisplacementMap", {{"scale", "6"}, {"in2", "n"}});
        } else if (id == "color-sepia")
            prim("feColorMatrix",
                 {{"values", "0.393 0.769 0.189 0 0 0.349 0.686 0.168 0 0 "
                             "0.272 0.534 0.131 0 0 0 0 0 1 0"}});
        else if (id == "color-duotone")
            prim("feColorMatrix", {{"type", "saturate"}, {"values", "0"}}, "g");
        else if (id == "tile-mosaic")
            prim("feConvolveMatrix",
                 {{"order", "3"}, {"divisor", "9"},
                  {"kernelMatrix", "1 1 1 1 1 1 1 1 1"}});
        else if (id == "tile-kaleido") {
            prim("feTurbulence",
                 {{"baseFrequency", "0.1"}, {"numOctaves", "2"}}, "n");
            prim("feDisplacementMap", {{"scale", "20"}, {"in2", "n"}});
        } else
            prim("feGaussianBlur", {{"stdDeviation", "2"}});
        // Duotone finishes warm over the desaturated base.
        if (id == "color-duotone")
            prim("feFlood", {{"flood-color", "#c9a227"}, {"flood-opacity", "0.25"}});
        if (id == "shadow-inner")
            prim("feComposite", {{"operator", "in"}, {"in2", "SourceGraphic"}});
        return g;
    };
    col->addWidget(makePanelFooter(
        state, {{QStringLiteral("filter-apply"), QObject::tr("Apply filter")}}, w,
        [state, list, recipe, &catalog](const QString&) {
            const int row = list->currentRow();
            if (row < 0 || row >= (int)catalog.size()) {
                state->setStatusHint(QObject::tr("Pick a filter first."));
                return;
            }
            vector::FilterGraph g = recipe(catalog[(size_t)row].id);
            DocumentItem* d = state->activeDocument();
            if (!d) return;
            // Vector art keeps the graph (export + re-edit round-trip).
            const int vindex = vectorEditableLayer(state);
            if (vindex >= 0 && vindex < d->layers.size()) {
                const LayerItem& l = d->layers[vindex];
                if (l.art && !l.art->isEmpty()) {
                    vector::ArtNode work = *l.art;
                    work.paint.hasFilter = true;
                    work.paint.filter = g;
                    if (state->applyVectorNode(vindex, work,
                                               QObject::tr("Filter"))) {
                        state->setStatusHint(QObject::tr("Filter attached."));
                        return;
                    }
                }
            }
            // Otherwise raster-run the active pixel layer, whole-layer.
            LayerItem* layer = state->activeLayer();
            if (!layer || layer->locked ||
                layer->kind != LayerItem::Kind::Pixel || !layer->pixels) {
                state->setStatusHint(QObject::tr("Select a pixel or vector layer."));
                return;
            }
            const int iw = (int)layer->pixels->width();
            const int ih = (int)layer->pixels->height();
            vector::RgbaImage src{iw, ih, std::vector<std::uint8_t>((size_t)iw * ih * 4)};
            for (int y = 0; y < ih; y++)
                for (int x = 0; x < iw; x++) {
                    const pittore::RGBAf& px =
                        layer->pixels->at((std::uint32_t)x, (std::uint32_t)y);
                    size_t o = (size_t)(y * iw + x) * 4;
                    src.px[o] = (std::uint8_t)(std::clamp(px.r, 0.0f, 1.0f) * 255);
                    src.px[o + 1] = (std::uint8_t)(std::clamp(px.g, 0.0f, 1.0f) * 255);
                    src.px[o + 2] = (std::uint8_t)(std::clamp(px.b, 0.0f, 1.0f) * 255);
                    src.px[o + 3] = (std::uint8_t)(std::clamp(px.a, 0.0f, 1.0f) * 255);
                }
            const vector::RgbaImage dst = vector::applyFilterGraph(g, src);
            if (dst.w != iw || dst.h != ih) return;
            d->beginUndoAction();
            if (!state->copyOnWriteActiveLayer()) {
                d->discardUndoAction();
                return;
            }
            layer = state->activeLayer();
            if (!layer || !layer->pixels) {
                d->discardUndoAction();
                return;
            }
            pittore::RGBAf* out = layer->pixels->data();
            for (int i = 0; i < iw * ih; i++) {
                size_t o = (size_t)i * 4;
                out[i].r = dst.px[o] / 255.0f;
                out[i].g = dst.px[o + 1] / 255.0f;
                out[i].b = dst.px[o + 2] / 255.0f;
                out[i].a = dst.px[o + 3] / 255.0f;
            }
            ++layer->sourceStamp;
            layer->thumbnail = QImage();
            d->rebuildComposite();
            d->commitUndoAction(QObject::tr("Filter"), QStringLiteral("filter"));
            emit state->historyChanged();
            emit state->documentModified(d);
            state->setStatusHint(QObject::tr("Filter applied."));
        }));
    return w;
}

QWidget* createSymbolsPanel(AppState* state, QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* list = new QListWidget(w);
    list->addItems({"Symbols: arrow-set", "Symbols: flowchart", "Clones: tiled-grid",
                    "Clones: radial", "Clones: spiral"});
    col->addWidget(list, 1);
    // Tiled clones of the editable layer (arrangePositions lattice, one undo
    // step per tile through the shared commit tail).
    auto* tileForm = new QFormLayout();
    auto* tileKind = new QComboBox(w);
    tileKind->addItems({QObject::tr("Grid"), QObject::tr("Radial"),
                        QObject::tr("Spiral"), QObject::tr("Honeycomb")});
    auto* tileCount = new QSpinBox(w);
    tileCount->setRange(2, 64);
    tileCount->setValue(8);
    auto* tileGap = new QDoubleSpinBox(w);
    tileGap->setRange(4.0, 500.0);
    tileGap->setValue(24.0);
    tileForm->addRow(QObject::tr("Lattice"), tileKind);
    tileForm->addRow(QObject::tr("Copies"), tileCount);
    tileForm->addRow(QObject::tr("Spacing"), tileGap);
    col->addLayout(tileForm);
    // Pattern tile transform for the editable vector layer (scale + rotate
    // compose over the pattern def matrix; one undo step on Apply).
    auto* form = new QFormLayout();
    auto* scale = new QDoubleSpinBox(w);
    scale->setRange(0.05, 20.0);
    scale->setSingleStep(0.05);
    scale->setValue(1.0);
    auto* rot = new QDoubleSpinBox(w);
    rot->setRange(-180.0, 180.0);
    rot->setValue(0.0);
    form->addRow(QObject::tr("Pattern scale"), scale);
    form->addRow(QObject::tr("Pattern rotation°"), rot);
    col->addLayout(form);
    col->addWidget(makePanelFooter(
        state,
        {{QStringLiteral("tile"), QObject::tr("Tile clones")},
         {QStringLiteral("pattern-apply"), QObject::tr("Apply pattern")}},
        w, [state, scale, rot, tileKind, tileCount, tileGap](const QString& id) {
            if (id == QStringLiteral("tile")) {
                DocumentItem* d = state->activeDocument();
                if (!d) return;
                const int index = vectorEditableLayer(state);
                if (index < 0 || index >= d->layers.size()) {
                    state->setStatusHint(QObject::tr("Select a vector shape layer."));
                    return;
                }
                const LayerItem& l = d->layers[index];
                if (!l.art || l.art->isEmpty()) {
                    state->setStatusHint(QObject::tr("Select a vector shape layer."));
                    return;
                }
                vector::ArrangeSpec spec;
                switch (tileKind->currentIndex()) {
                    case 1: spec.kind = vector::ArrangeKind::Circle; break;
                    case 2: spec.kind = vector::ArrangeKind::Spiral; break;
                    case 3: spec.kind = vector::ArrangeKind::Honeycomb; break;
                    default: spec.kind = vector::ArrangeKind::Grid; break;
                }
                const int n = tileCount->value();
                spec.cols = (int)std::ceil(std::sqrt(n));
                spec.rows = spec.cols;
                spec.dx = spec.dy = tileGap->value();
                spec.radius = tileGap->value() * 2;
                const auto cells = vector::arrangePositions(n, spec);
                const QRectF base = layerBounds(*d, l);
                int made = 0;
                for (int k = 1; k < (int)cells.size(); k++) {
                    auto node = std::make_shared<vector::ArtNode>(*l.art);
                    node->name = l.name.toStdString() + "-t" + std::to_string(k);
                    const QRectF frame =
                        base.translated(cells[(size_t)k].first - cells[0].first,
                                        cells[(size_t)k].second - cells[0].second);
                    if (state->commitArtNodeLayer(node, frame, ToolId::ShapeBuilderTool,
                                                  QObject::tr("Tile Clones"), false))
                        made++;
                }
                state->setStatusHint(made > 0 ? QObject::tr("%1 clones tiled.").arg(made)
                                              : QObject::tr("Nothing tiled."));
                return;
            }
            DocumentItem* d = state->activeDocument();
            if (!d) return;
            const int index = vectorEditableLayer(state);
            if (index < 0 || index >= d->layers.size()) {
                state->setStatusHint(QObject::tr("Select a vector shape layer."));
                return;
            }
            const LayerItem& l = d->layers[index];
            if (!l.art || l.art->isEmpty()) {
                state->setStatusHint(QObject::tr("Select a vector shape layer."));
                return;
            }
            pittore::vector::ArtNode work = *l.art;
            if (work.paint.patternId.empty()) {
                state->setStatusHint(QObject::tr("Layer has no pattern fill."));
                return;
            }
            const double s = scale->value();
            const double a = rot->value() * 3.14159265358979 / 180.0;
            work.paint.hasPatternXform = true;
            work.paint.patternXform[0] = s * std::cos(a);
            work.paint.patternXform[1] = s * std::sin(a);
            work.paint.patternXform[2] = -s * std::sin(a);
            work.paint.patternXform[3] = s * std::cos(a);
            work.paint.patternXform[4] = 0.0;
            work.paint.patternXform[5] = 0.0;
            if (state->applyVectorNode(index, work, QObject::tr("Pattern Transform")))
                state->setStatusHint(QObject::tr("Pattern transform applied."));
        }));
    return w;
}

QWidget* createDocPropsPanel(AppState* state, QWidget* parent) {
    // Live document readout (size, layers, guides, color tag). Canvas-only
    // settings (grid step, bleed guides) stay in View, not the document.
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* form = new QFormLayout();
    auto* size = new QLabel(w);
    auto* layers = new QLabel(w);
    auto* guides = new QLabel(w);
    auto* mode = new QLabel(w);
    form->addRow(QObject::tr("Size"), size);
    form->addRow(QObject::tr("Layers"), layers);
    form->addRow(QObject::tr("Guides"), guides);
    form->addRow(QObject::tr("Mode"), mode);
    col->addLayout(form);
    col->addStretch(1);
    auto refresh = [state, size, layers, guides, mode] {
        const DocumentItem* d = state->activeDocument();
        if (!d) {
            size->setText(QObject::tr("—"));
            layers->setText(QObject::tr("—"));
            guides->setText(QObject::tr("—"));
            mode->setText(QObject::tr("—"));
            return;
        }
        size->setText(QObject::tr("%1 × %2").arg(d->size.width()).arg(d->size.height()));
        layers->setText(QString::number(d->layers.size()));
        guides->setText(QString::number(d->horizontalGuides.size() +
                                        d->verticalGuides.size()));
        mode->setText(d->colorMode.isEmpty() ? QObject::tr("RGB") : d->colorMode);
    };
    refresh();
    col->addWidget(makePanelFooter(
        state, {{QStringLiteral("doc-refresh"), QObject::tr("Refresh")}}, w,
        [refresh](const QString&) { refresh(); }));
    return w;
}

QWidget* createTracePanel(AppState* state, QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* form = new QFormLayout(w);
    auto* mode = new QComboBox(w);
    mode->addItems({QObject::tr("Monochrome"), QObject::tr("Color"), QObject::tr("Centerline")});
    auto* thresh = new QDoubleSpinBox(w);
    thresh->setRange(0, 1);
    thresh->setSingleStep(0.01);
    thresh->setValue(0.5);
    form->addRow(QObject::tr("Mode"), mode);
    form->addRow(QObject::tr("Threshold"), thresh);
    auto* footer = makePanelFooter(state, {{QStringLiteral("trace"), QObject::tr("Trace")}}, w,
                                   [state, mode, thresh](const QString&) {
            DocumentItem* d = state->activeDocument();
            if (!d) return;
            // Trace the active pixel layer (or the composite selection bbox
            // when no pixel layer is active) into one vector layer.
            const LayerItem* src = nullptr;
            for (const LayerItem& l : d->layers) {
                const LayerDrawSource ds = layerDrawSource(l);
                if (ds.img && ds.img->width() > 0 && ds.img->height() > 0) {
                    src = &l;
                    break;
                }
            }
            if (!src) {
                state->setStatusHint(QObject::tr("Trace: no raster layer."));
                return;
            }
            const LayerDrawSource ds = layerDrawSource(*src);
            const int sw = (int)ds.img->width(), sh = (int)ds.img->height();
            std::vector<std::uint8_t> rgba((size_t)sw * sh * 4);
            for (int y = 0; y < sh; y++)
                for (int x = 0; x < sw; x++) {
                    const pittore::RGBAf& px =
                        ds.img->at((std::uint32_t)x, (std::uint32_t)y);
                    size_t o = (size_t)(y * sw + x) * 4;
                    rgba[o] = (std::uint8_t)(std::clamp(px.r, 0.0f, 1.0f) * 255);
                    rgba[o + 1] = (std::uint8_t)(std::clamp(px.g, 0.0f, 1.0f) * 255);
                    rgba[o + 2] = (std::uint8_t)(std::clamp(px.b, 0.0f, 1.0f) * 255);
                    rgba[o + 3] = (std::uint8_t)(std::clamp(px.a, 0.0f, 1.0f) * 255);
                }
            vector::TraceSpec spec;
            spec.mode = mode->currentIndex() == 1 ? vector::TraceMode::Color
                      : mode->currentIndex() == 2 ? vector::TraceMode::Centerline
                                                  : vector::TraceMode::Mono;
            spec.threshold = thresh->value();
            const auto loops = vector::traceRgba(rgba, sw, sh, spec);
            if (loops.empty()) {
                state->setStatusHint(QObject::tr("Trace: nothing found."));
                return;
            }
            // Loops are image-space: map through the source placement.
            std::vector<std::vector<vector::Segment>> moved;
            for (auto segs : loops) {
                for (auto& s : segs) {
                    if (s.kind == vector::Segment::Kind::Close) continue;
                    s.x = (float)(ds.offset.x() + s.x * ds.scaleX);
                    s.y = (float)(ds.offset.y() + s.y * ds.scaleY);
                    if (s.kind == vector::Segment::Kind::CubicTo) {
                        s.c1x = (float)(ds.offset.x() + s.c1x * ds.scaleX);
                        s.c1y = (float)(ds.offset.y() + s.c1y * ds.scaleY);
                        s.c2x = (float)(ds.offset.x() + s.c2x * ds.scaleX);
                        s.c2y = (float)(ds.offset.y() + s.c2y * ds.scaleY);
                    }
                }
                moved.push_back(segs);
            }
            if (state->addVectorPathLayer(vector::combinePaths(moved),
                                          ToolId::ContentAwareTracing,
                                          QObject::tr("Trace Bitmap")))
                state->setStatusHint(
                    QObject::tr("Traced %1 paths.").arg(loops.size()));
        });
    form->addWidget(footer);
    return w;
}

QWidget* createExtensionsPanel(AppState* state, QWidget* parent) {
    // Script extensions: *.xml/*.inx descriptors in the user extensions dir
    // run as subprocesses (SVG on stdin, SVG on stdout) and re-import as a
    // new document. Real end-to-end execution with timeout + stderr capture.
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* list = new QListWidget(w);
    auto rebuild = [state, list] {
        list->clear();
        QDir dir(extensionDirectory());
        if (!dir.exists()) return;
        const QStringList files =
            dir.entryList({"*.xml", "*.inx"}, QDir::Files, QDir::Name);
        for (const QString& f : files) {
            QFile in(dir.filePath(f));
            if (!in.open(QIODevice::ReadOnly)) continue;
            bool ok = false;
            vector::ExtensionDef ext = vector::parseExtension(
                in.readAll().toStdString(), ok);
            if (!ok || ext.id.empty()) continue;
            auto* item = new QListWidgetItem(
                QString("%1 — %2")
                    .arg(QString::fromStdString(ext.name.empty() ? ext.id : ext.name),
                         QString::fromStdString(ext.id)),
                list);
            item->setData(Qt::UserRole, dir.filePath(f));
        }
    };
    rebuild();
    col->addWidget(list, 1);
    col->addWidget(makePanelFooter(
        state,
        {{QStringLiteral("ext-refresh"), QObject::tr("Refresh")},
         {QStringLiteral("ext-run"), QObject::tr("Run")}},
        w, [state, list, rebuild](const QString& id) {
            if (id == QStringLiteral("ext-refresh")) {
                rebuild();
                return;
            }
            const auto sel = list->selectedItems();
            if (sel.isEmpty()) {
                state->setStatusHint(QObject::tr("Pick an extension first."));
                return;
            }
            QFile in(sel.first()->data(Qt::UserRole).toString());
            if (!in.open(QIODevice::ReadOnly)) return;
            bool ok = false;
            vector::ExtensionDef ext =
                vector::parseExtension(in.readAll().toStdString(), ok);
            if (!ok) {
                state->setStatusHint(QObject::tr("Bad extension descriptor."));
                return;
            }
            DocumentItem* d = state->activeDocument();
            if (!d) {
                state->setStatusHint(QObject::tr("Open a document first."));
                return;
            }
            // Serialize the active document to a temp SVG for stdin.
            QTemporaryFile tmp(QDir::tempPath() + "/pittore-ext-XXXXXX.svg");
            tmp.setAutoRemove(true);
            if (!tmp.open()) {
                state->setStatusHint(QObject::tr("Cannot stage input."));
                return;
            }
            const QString tmpPath = tmp.fileName();
            tmp.close();
            ExportSettings settings;
            settings.format = QStringLiteral("svg");
            QString err;
            if (!writeExportVector(*d, settings, tmpPath, &err)) {
                state->setStatusHint(err.isEmpty() ? QObject::tr("Export failed.")
                                                   : err);
                return;
            }
            // Defaults for every declared param (a param editor is follow-up).
            std::map<std::string, std::string> args;
            for (const auto& pr : ext.params) args[pr.name] = pr.def;
            std::string command = ext.script;
            for (const auto& [k, v] : args) command += " --" + k + "=\"" + v + "\"";
            const QStringList parts =
                QProcess::splitCommand(QString::fromStdString(command));
            if (parts.isEmpty()) {
                state->setStatusHint(QObject::tr("Empty script command."));
                return;
            }
            QProcess proc(list);
            proc.setStandardInputFile(tmpPath);
            proc.start(parts.first(), parts.mid(1));
            if (!proc.waitForStarted(5000)) {
                state->setStatusHint(QObject::tr("Could not start: %1")
                                         .arg(proc.errorString()));
                return;
            }
            if (!proc.waitForFinished(30000)) {
                proc.kill();
                state->setStatusHint(QObject::tr("Extension timed out."));
                return;
            }
            const QByteArray errBytes = proc.readAllStandardError();
            const QByteArray outBytes = proc.readAllStandardOutput();
            if (proc.exitCode() != 0 || outBytes.trimmed().isEmpty()) {
                QMessageBox::warning(
                    list, QObject::tr("Extension failed"),
                    QString::fromUtf8(errBytes.isEmpty() ? outBytes : errBytes)
                        .left(2000));
                return;
            }
            int dpi = 96;
            SvgImportResult result;
            if (!svgPartsImport(outBytes, &result, &dpi, &err)) {
                state->setStatusHint(err.isEmpty() ? QObject::tr("Bad output SVG.")
                                                   : err);
                return;
            }
            if (!state->openSvgParts(QObject::tr("Extension output"), result, dpi,
                                     &err))
                state->setStatusHint(err.isEmpty() ? QObject::tr("Import failed.")
                                                   : err);
            else
                state->setStatusHint(QObject::tr("Extension output opened."));
        }));
    return w;
}

QWidget* createPagesPanel(AppState* state, QWidget* parent) {
    // Live page frames (Page* layers, else whole canvas) + multipage PDF.
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* list = new QListWidget(w);
    auto rebuild = [state, list] {
        list->clear();
        const DocumentItem* d = state->activeDocument();
        if (!d) return;
        int k = 0;
        for (const QRectF& f : pageFrames(*d))
            list->addItem(QObject::tr("Page %1 (%2×%3)")
                              .arg(++k)
                              .arg(qRound(f.width()))
                              .arg(qRound(f.height())));
    };
    rebuild();
    col->addWidget(list, 1);
    col->addWidget(makePanelFooter(
        state,
        {{QStringLiteral("pages-refresh"), QObject::tr("Refresh")},
         {QStringLiteral("pages-pdf"), QObject::tr("Export PDF…")}},
        w, [state, list, rebuild](const QString& id) {
            if (id == QStringLiteral("pages-refresh")) {
                rebuild();
                return;
            }
            const QString path = QFileDialog::getSaveFileName(
                list, QObject::tr("Export pages to PDF"), QStringLiteral("pages.pdf"),
                QObject::tr("PDF (*.pdf)"));
            if (path.isEmpty()) return;
            QString err;
            if (!exportPagesPdf(state, state->activeDocumentIndex(), path, &err))
                state->setStatusHint(err);
            else
                state->setStatusHint(QObject::tr("Pages exported."));
        }));
    return w;
}

QWidget* createMarkersPanel(AppState* state, QWidget* parent) {
    // Apply a builtin marker (mk-<name>) to the editable layer's ends.
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* list = new QListWidget(w);
    for (auto& id : vector::builtinMarkerIds())
        list->addItem(QString::fromStdString(id));
    col->addWidget(list, 1);
    auto* form = new QFormLayout();
    auto* pos = new QComboBox(w);
    pos->addItems({QObject::tr("Start"), QObject::tr("Mid"),
                   QObject::tr("End"), QObject::tr("All")});
    pos->setCurrentIndex(3);
    form->addRow(QObject::tr("Attach to"), pos);
    col->addLayout(form);
    col->addWidget(makePanelFooter(
        state, {{QStringLiteral("marker-apply"), QObject::tr("Apply")}}, w,
        [state, list, pos](const QString&) {
            const int row = list->currentRow();
            const auto ids = vector::builtinMarkerIds();
            if (row < 0 || row >= (int)ids.size()) {
                state->setStatusHint(QObject::tr("Pick a marker first."));
                return;
            }
            DocumentItem* d = state->activeDocument();
            if (!d) return;
            const int index = vectorEditableLayer(state);
            if (index < 0 || index >= d->layers.size()) {
                state->setStatusHint(QObject::tr("Select a vector shape layer."));
                return;
            }
            const LayerItem& l = d->layers[index];
            if (!l.art || l.art->isEmpty()) {
                state->setStatusHint(QObject::tr("Select a vector shape layer."));
                return;
            }
            pittore::vector::ArtNode work = *l.art;
            const std::string id = "mk-" + ids[(size_t)row];
            const int p = pos->currentIndex();
            if (p == 0 || p == 3) work.paint.markerStart = id;
            if (p == 1 || p == 3) work.paint.markerMid = id;
            if (p == 2 || p == 3) work.paint.markerEnd = id;
            if (state->applyVectorNode(index, work, QObject::tr("Markers")))
                state->setStatusHint(QObject::tr("Marker applied."));
        }));
    return w;
}

QWidget* createLpePanel(AppState* state, QWidget* parent) {
    // Numeric knotholder: pick an effect, set amount/size, Apply runs the
    // shared engine drag-params (60px synthetic drag) on the editable layer.
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 4, 0, 0);
    auto* list = new QListWidget(w);
    for (auto& info : vector::lpe::allEffects())
        list->addItem(QString::fromStdString(std::string(info.label) +
                                             (info.experimental ? " (experimental)" : "")));
    col->addWidget(list, 1);
    auto* form = new QFormLayout();
    auto* amount = new QDoubleSpinBox(w);
    amount->setRange(0.0, 500.0);
    amount->setValue(30.0);
    auto* size = new QDoubleSpinBox(w);
    size->setRange(4.0, 500.0);
    size->setValue(60.0);
    form->addRow(QObject::tr("Amount %"), amount);
    form->addRow(QObject::tr("Drag px"), size);
    col->addLayout(form);
    col->addWidget(makePanelFooter(
        state, {{QStringLiteral("lpe-apply"), QObject::tr("Apply effect")}}, w,
        [state, list, amount, size](const QString&) {
            const int row = list->currentRow();
            const auto& all = vector::lpe::allEffects();
            if (row < 0 || row >= (int)all.size()) {
                state->setStatusHint(QObject::tr("Pick a path effect first."));
                return;
            }
            DocumentItem* d = state->activeDocument();
            if (!d) return;
            const int index = vectorEditableLayer(state);
            if (index < 0 || index >= d->layers.size()) {
                state->setStatusHint(QObject::tr("Select a vector shape layer."));
                return;
            }
            const LayerItem& l = d->layers[index];
            if (!l.art || l.art->isEmpty()) {
                state->setStatusHint(QObject::tr("Select a vector shape layer."));
                return;
            }
            // Synthetic drag in node space: rightward, `size` long.
            vector::ArtNode work = *l.art;
            vector::lpe::Params p = vector::lpe::dragParamsFor(
                all[(size_t)row].type, 0.0, 0.0, size->value(), 0.0, amount->value());
            if (auto fx = vector::lpe::makeEffect(all[(size_t)row].type, p)) {
                work.segments = fx->apply(work.segments);
                if (state->applyVectorNode(index, work, QObject::tr("Path Effect")))
                    state->setStatusHint(QObject::tr("Effect applied."));
            }
        }));
    return w;
}

}  // namespace pittore::ui
