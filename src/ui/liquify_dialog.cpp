#include "ui/liquify_dialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTabletEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabletEvent>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QShowEvent>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>

#include "ui/app_state.h"
#include "ui/icons.h"
#include "ui/theme.h"

namespace pittore::ui {

namespace {

const LiquifyToolDef kTools[] = {
    {"Forward Warp", "W", 0},
    {"Reconstruct", "R", 1},
    {"Smooth", "E", 2},
    {"Twirl CW", "C", 3},
    {"Twirl CCW", "Alt+C", 4},
    {"Pucker", "S", 5},
    {"Bloat", "B", 6},
    {"Push Left", "O", 7},
    {"Push Right", "Alt+O", 8},
    {"Mirror", "M", 9},
    {"Turbulence", "T", 10},
    {"Mesh Clone", "K", 11},
    {"Freeze", "F", 12},
    {"Thaw", "D", 13},
    {"Hand", "H", 14},
    {"Zoom", "Z", 15},
};

std::uint64_t hashPair(int x, int y, std::uint64_t seed) {
    std::uint64_t z = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 374761393ULL +
                      static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 668265263ULL + seed;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

QColor colorByName(const QString &name, QColor fallback) {
    static const QMap<QString, QColor> table = {
        {QStringLiteral("Red"), QColor(255, 0, 0)},
        {QStringLiteral("Green"), QColor(0, 255, 0)},
        {QStringLiteral("Blue"), QColor(0, 120, 255)},
        {QStringLiteral("Cyan"), QColor(0, 200, 255)},
        {QStringLiteral("Yellow"), QColor(255, 255, 0)},
        {QStringLiteral("Magenta"), QColor(255, 0, 255)},
        {QStringLiteral("Black"), QColor(0, 0, 0)},
        {QStringLiteral("White"), QColor(255, 255, 255)},
    };
    return table.value(name, fallback);
}

QImage imageFromLayerPixels(const pittore::Image &img) {
    QImage out(static_cast<int>(img.width()), static_cast<int>(img.height()),
               QImage::Format_ARGB32_Premultiplied);
    for (std::uint32_t y = 0; y < img.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(static_cast<int>(y)));
        for (std::uint32_t x = 0; x < img.width(); ++x) {
            const pittore::RGBAf &p = img.at(x, y);
            const float a = std::clamp(p.a, 0.0f, 1.0f);
            row[x] = qRgba(static_cast<int>(std::clamp(p.r, 0.0f, 1.0f) * a * 255.0f),
                           static_cast<int>(std::clamp(p.g, 0.0f, 1.0f) * a * 255.0f),
                           static_cast<int>(std::clamp(p.b, 0.0f, 1.0f) * a * 255.0f),
                           static_cast<int>(a * 255.0f));
        }
    }
    return out;
}

bool isRadialTool(int tool) {
    return tool == 1 || tool == 2 || tool == 3 || tool == 4 || tool == 5 ||
           tool == 6 || tool == 10;
}

bool isDragTool(int tool) {
    return tool == 0 || tool == 7 || tool == 8 || tool == 9 || tool == 11;
}

}

const LiquifyToolDef *liquifyTools(int &count) {
    count = static_cast<int>(sizeof(kTools) / sizeof(kTools[0]));
    return kTools;
}

LiquifyCanvas::LiquifyCanvas(AppState *state, QWidget *parent)
    : QWidget(parent), state_(state) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    bg_ = colorsFor(state_->theme()).surround;
    connect(state_, &AppState::themeChanged, this, [this] {
        bg_ = colorsFor(state_->theme()).surround;
        update();
    });
    connect(&holdTimer_, &QTimer::timeout, this, [this] {
        if (pressed_ && isRadialTool(brush_.tool)) applyDab(lastDoc_, 0.0f, 0.0f);
    });
    holdTimer_.setInterval(50);
}

void LiquifyCanvas::setZoom(double z, QPointF anchor) {
    const double nz = std::clamp(z, 0.05, 32.0);
    if (anchor.x() >= 0 && nz != zoom_) {
        const QPointF before = (anchor - pan_) / zoom_;
        pan_ = anchor - before * nz;
    }
    zoom_ = nz;
    update();
}

void LiquifyCanvas::zoomToFit() {
    DocumentItem *d = state_->activeDocument();
    if (!d || d->size.isEmpty()) return;
    const double zx = width() / std::max(1.0, double(d->size.width()));
    const double zy = height() / std::max(1.0, double(d->size.height()));
    zoom_ = std::clamp(std::min(zx, zy), 0.05, 32.0);
    pan_ = QPointF((width() - d->size.width() * zoom_) * 0.5,
                   (height() - d->size.height() * zoom_) * 0.5);
    update();
    emit statusChanged();
}

void LiquifyCanvas::zoomToWidth() {
    DocumentItem *d = state_->activeDocument();
    if (!d || d->size.isEmpty() || d->size.width() <= 0) return;
    zoom_ = std::clamp(width() / double(d->size.width()), 0.05, 32.0);
    pan_.setX((width() - d->size.width() * zoom_) * 0.5);
    pan_.setY((height() - d->size.height() * zoom_) * 0.5);
    update();
    emit statusChanged();
}

void LiquifyCanvas::zoomIn() {
    setZoom(zoom_ * 1.25);
    emit statusChanged();
}

void LiquifyCanvas::zoomOut() {
    setZoom(zoom_ * 0.8);
    emit statusChanged();
}

void LiquifyCanvas::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    QAction *zoomInAction = menu.addAction(tr("Zoom In"));
    QAction *zoomOutAction = menu.addAction(tr("Zoom Out"));
    menu.addSeparator();
    QAction *fitAction = menu.addAction(tr("Fit on Screen"));
    QAction *fitWidthAction = menu.addAction(tr("Fit to Width"));
    menu.addSeparator();
    QAction *pct50 = menu.addAction(tr("50%"));
    QAction *pct100 = menu.addAction(tr("100%"));
    QAction *pct150 = menu.addAction(tr("150%"));
    QAction *pct200 = menu.addAction(tr("200%"));
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen) return;
    if (chosen == zoomInAction)
        zoomIn();
    else if (chosen == zoomOutAction)
        zoomOut();
    else if (chosen == fitAction)
        zoomToFit();
    else if (chosen == fitWidthAction)
        zoomToWidth();
    else if (chosen == pct50)
        setZoom(0.5);
    else if (chosen == pct100)
        setZoom(1.0);
    else if (chosen == pct150)
        setZoom(1.5);
    else if (chosen == pct200)
        setZoom(2.0);
    emit statusChanged();
}

QPointF LiquifyCanvas::viewToDoc(QPointF vp) const {
    return QPointF((vp.x() - pan_.x()) / zoom_, (vp.y() - pan_.y()) / zoom_);
}

QPointF LiquifyCanvas::docToView(QPointF dp) const {
    return QPointF(dp.x() * zoom_ + pan_.x(), dp.y() * zoom_ + pan_.y());
}

QPointF LiquifyCanvas::viewToLayer(QPointF vp) const {
    LayerItem *l = state_->activeLayer();
    if (!l) return QPointF(0, 0);
    const QPointF dp = viewToDoc(vp);
    const double lsx = std::max(l->scaleX, 1e-6);
    const double lsy = std::max(l->scaleY, 1e-6);
    return QPointF((dp.x() - l->offset.x()) / lsx, (dp.y() - l->offset.y()) / lsy);
}

QPointF LiquifyCanvas::layerToDoc(QPointF lp) const {
    LayerItem *l = state_->activeLayer();
    if (!l) return lp;
    return QPointF(l->offset.x() + lp.x() * l->scaleX,
                   l->offset.y() + lp.y() * l->scaleY);
}

float LiquifyCanvas::falloffExp() const {
    const double dh = (brush_.density + brush_.hardness) * 0.5;
    static const double rampExp[3] = {1.0, 0.55, 1.8};
    const int ramp = std::clamp(brush_.ramp, 0, 2);
    return static_cast<float>(rampExp[ramp] * (2.5 - dh * 0.02));
}

float LiquifyCanvas::dabStrength(bool radial) const {
    float s = static_cast<float>(brush_.pressure / 100.0 * brush_.opacity / 100.0 *
                                 (0.25 + 1.5 * brush_.rate / 100.0) *
                                 (0.25 + 1.5 * brush_.speed / 100.0));
    if (brush_.stylus) s *= static_cast<float>(std::clamp(tabletPressure_, 0.05, 1.0));
    (void)radial;
    return s;
}

float LiquifyCanvas::brushRadiusLayer() const {
    LayerItem *l = state_->activeLayer();
    if (!l) return 0.0f;
    const double ls = std::min(std::max(l->scaleX, 1e-6), std::max(l->scaleY, 1e-6));
    return static_cast<float>(brush_.size / 2.0 / ls);
}

bool LiquifyCanvas::ensureSession() {
    if (sessionOk_ && state_->liquifySessionActive()) return true;
    sessionOk_ = state_->beginLiquifySession();
    return sessionOk_;
}

void LiquifyCanvas::beginSessionUndoPoint() {
    undoStack_.clear();
}

void LiquifyCanvas::pushStrokeUndoPoint() {
    if (!state_->liquifySessionActive()) return;
    StrokeUndo u;
    u.mesh = state_->liquifySessionMesh().offsets;
    u.mask = state_->liquifyMaskCopy();
    undoStack_.push_back(std::move(u));
    while (undoStack_.size() > 20) undoStack_.erase(undoStack_.begin());
}

bool LiquifyCanvas::undoStroke() {
    if (undoStack_.empty() || !state_->liquifySessionActive()) return false;
    StrokeUndo u = std::move(undoStack_.back());
    undoStack_.pop_back();
    state_->liquifySessionMesh().offsets = std::move(u.mesh);
    state_->restoreLiquifyMask(std::move(u.mask));
    if (state_->renderLiquifyFull()) {
        sessionMoved_ = true;
        refreshImages();
        bumpMaskVersion();
        update();
        return true;
    }
    return false;
}

void LiquifyCanvas::refreshImages() {
    LayerItem *l = state_->activeLayer();
    DocumentItem *d = state_->activeDocument();
    if (!l || !l->pixels || !d) return;
    if (layerCache_.isNull() || layerCache_.size() != QSize(int(l->pixels->width()), int(l->pixels->height())))
        layerCache_ = imageFromLayerPixels(*l->pixels);
    else {
        const QRect dirty = lastDirtyRect_.isNull()
            ? QRect(0, 0, layerCache_.width(), layerCache_.height())
            : lastDirtyRect_;
        for (int y = std::max(0, dirty.top()); y < std::min(layerCache_.height(), dirty.bottom() + 1); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(layerCache_.scanLine(y));
            for (int x = std::max(0, dirty.left()); x < std::min(layerCache_.width(), dirty.right() + 1); ++x) {
                const pittore::RGBAf &p = l->pixels->at(std::uint32_t(x), std::uint32_t(y));
                const float a = std::clamp(p.a, 0.0f, 1.0f);
                row[x] = qRgba(int(std::clamp(p.r, 0.0f, 1.0f) * a * 255.0f),
                               int(std::clamp(p.g, 0.0f, 1.0f) * a * 255.0f),
                               int(std::clamp(p.b, 0.0f, 1.0f) * a * 255.0f),
                               int(a * 255.0f));
            }
        }
    }
    lastDirtyRect_ = QRect();
}

void LiquifyCanvas::applyDab(QPointF docPos, float dxLayer, float dyLayer) {
    LayerItem *l = state_->activeLayer();
    if (!l || !l->pixels) return;
    if (!ensureSession()) return;
    if (state_->liquifySessionMesh().cols < 2) return;
    const double lsx = std::max(l->scaleX, 1e-6);
    const double lsy = std::max(l->scaleY, 1e-6);
    const float cx = static_cast<float>((docPos.x() - l->offset.x()) / lsx);
    const float cy = static_cast<float>((docPos.y() - l->offset.y()) / lsy);
    const float radius = brushRadiusLayer();
    if (radius <= 0.0f) return;
    const float strength = dabStrength(isRadialTool(brush_.tool));
    if (strength <= 0.0f) return;
    const float exp = falloffExp();
    const float jitter = brush_.jitter / 100.0f;
    static const float reconFactor[4] = {1.0f, 0.45f, 0.7f, 0.9f};
    const int reconMode = std::clamp(brush_.reconstructMode, 0, 3);
    const float pinMargin = 2.0f * pittore::compute::kWarpCell;
    pittore::compute::WarpMesh &mesh = state_->liquifySessionMesh();
    const auto pinW = [&](float vx, float vy) {
        if (!brush_.pinEdges) return 1.0f;
        const float ex = std::min(vx - mesh.left, mesh.right - vx);
        const float ey = std::min(vy - mesh.top, mesh.bottom - vy);
        const float e = std::min(ex, ey) / pinMargin;
        if (e >= 1.0f) return 1.0f;
        if (e <= 0.0f) return 0.0f;
        return e * e * (3.0f - 2.0f * e);
    };
    const auto maskW = [&](float vx, float vy) {
        return 1.0f - state_->liquifyMaskAt(vx, vy);
    };
    const int tool = brush_.tool;
    const bool alt = QApplication::keyboardModifiers().testFlag(Qt::AltModifier);
    const int eff = (tool == 7 && alt) ? 8 : (tool == 8 && alt) ? 7
        : (tool == 3 && alt) ? 4 : (tool == 4 && alt) ? 3
        : (tool == 5 && alt) ? 6 : (tool == 6 && alt) ? 5 : tool;
    switch (eff) {
        case 0: {
            if (dxLayer == 0.0f && dyLayer == 0.0f) return;
            pittore::compute::warp_mesh_for_each_near_shaped(
                mesh, cx, cy, radius, exp,
                [&](float &ox, float &oy, float w, float rx, float ry) {
                    const float k = w * strength * pinW(cx + rx, cy + ry) *
                                    maskW(cx + rx, cy + ry);
                    ox -= dxLayer * k;
                    oy -= dyLayer * k;
                });
            break;
        }
        case 1: {
            const float amt = strength * 0.5f * reconFactor[reconMode];
            if (amt <= 0.0f) return;
            pittore::compute::warp_mesh_for_each_near_shaped(
                mesh, cx, cy, radius, exp,
                [&](float &ox, float &oy, float w, float rx, float ry) {
                    const float k = std::clamp(
                        w * amt * pinW(cx + rx, cy + ry) * maskW(cx + rx, cy + ry),
                        0.0f, 1.0f);
                    ox *= 1.0f - k;
                    oy *= 1.0f - k;
                });
            break;
        }
        case 2: {
            pittore::compute::warp_mesh_smooth(mesh, cx, cy, radius, strength);
            break;
        }
        case 3:
        case 4: {
            const float step = strength * 0.25f * (eff == 3 ? 1.0f : -1.0f);
            if (step == 0.0f) return;
            pittore::compute::warp_mesh_for_each_near_shaped(
                mesh, cx, cy, radius, exp,
                [&](float &ox, float &oy, float w, float rx, float ry) {
                    const float k = w * pinW(cx + rx, cy + ry) * maskW(cx + rx, cy + ry);
                    const float a = step * k;
                    const float s = std::sin(a), c = std::cos(a);
                    const float fx = rx + ox, fy = ry + oy;
                    ox = fx * c - fy * s - rx;
                    oy = fx * s + fy * c - ry;
                });
            break;
        }
        case 5:
        case 6: {
            const float step = strength * 0.15f * (eff == 5 ? 1.0f : -1.0f);
            if (step == 0.0f) return;
            pittore::compute::warp_mesh_for_each_near_shaped(
                mesh, cx, cy, radius, exp,
                [&](float &ox, float &oy, float w, float rx, float ry) {
                    const float k = w * pinW(cx + rx, cy + ry) * maskW(cx + rx, cy + ry);
                    const float fx = rx + ox, fy = ry + oy;
                    ox += fx * step * k;
                    oy += fy * step * k;
                });
            break;
        }
        case 7:
        case 8: {
            const float len = std::hypot(dxLayer, dyLayer);
            if (len < 1e-4f) return;
            float px = dyLayer / len, py = -dxLayer / len;
            if (eff == 8) {
                px = -px;
                py = -py;
            }
            const float push = len * strength;
            pittore::compute::warp_mesh_for_each_near_shaped(
                mesh, cx, cy, radius, exp,
                [&](float &ox, float &oy, float w, float rx, float ry) {
                    const float k = w * pinW(cx + rx, cy + ry) * maskW(cx + rx, cy + ry);
                    ox -= px * push * k;
                    oy -= py * push * k;
                });
            break;
        }
        case 10: {
            const float mag = radius * 0.12f * jitter * strength;
            if (mag <= 0.0f) return;
            ++dabSeed_;
            pittore::compute::warp_mesh_for_each_near_shaped(
                mesh, cx, cy, radius, exp,
                [&](float &ox, float &oy, float w, float rx, float ry) {
                    const int vx = static_cast<int>(std::round(cx + rx));
                    const int vy = static_cast<int>(std::round(cy + ry));
                    const std::uint64_t hh = hashPair(vx, vy, dabSeed_);
                    const double ang = (hh % 628318) / 100000.0;
                    const float k = w * pinW(cx + rx, cy + ry) * maskW(cx + rx, cy + ry);
                    ox += static_cast<float>(std::cos(ang)) * mag * k;
                    oy += static_cast<float>(std::sin(ang)) * mag * k;
                });
            break;
        }
        case 11: {
            if (!state_->liquifyHasCloneSource()) {
                state_->setStatusHint(tr("Alt-click to set the clone source first."));
                return;
            }
            const QPointF src = state_->liquifyCloneSourcePoint();
            const double lsx2 = std::max(l->scaleX, 1e-6);
            const double lsy2 = std::max(l->scaleY, 1e-6);
            const float sx = static_cast<float>((src.x() - l->offset.x()) / lsx2);
            const float sy = static_cast<float>((src.y() - l->offset.y()) / lsy2);
            pittore::compute::warp_mesh_clone(mesh, sx, sy, cx, cy, radius, strength);
            break;
        }
        default:
            return;
    }
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    pittore::compute::warp_dab_rect(mesh, cx, cy, radius, x0, y0, x1, y1);
    if (x0 >= x1 || y0 >= y1) return;
    if (state_->liquifyDab(mesh, x0, y0, x1, y1)) {
        strokeMoved_ = true;
        sessionMoved_ = true;
        state_->flushPaint();
        lastDirtyRect_ = lastDirtyRect_.isNull()
            ? QRect(x0, y0, x1 - x0, y1 - y0)
            : lastDirtyRect_.united(QRect(x0, y0, x1 - x0, y1 - y0));
        refreshImages();
        // Repaint the dab footprint plus the brush ring (plus margin), not
        // the whole widget.
        const QPointF a = docToView(layerToDoc(QPointF(x0, y0)));
        const QPointF b = docToView(layerToDoc(QPointF(x1, y1)));
        const double ringR =
            radius * std::max(lsx, lsy) * zoom_ + 8.0;
        const QPointF vc = docToView(docPos);
        update(QRectF(a, b)
                   .normalized()
                   .united(QRectF(vc.x() - ringR, vc.y() - ringR, 2 * ringR,
                                  2 * ringR))
                   .toAlignedRect());
        emit statusChanged();
    }
}

void LiquifyCanvas::applyMirrorDab(QPointF docPos) {
    LayerItem *l = state_->activeLayer();
    if (!l || !l->pixels) return;
    if (!ensureSession()) return;
    const double lsx = std::max(l->scaleX, 1e-6);
    const double lsy = std::max(l->scaleY, 1e-6);
    const float cx = static_cast<float>((docPos.x() - l->offset.x()) / lsx);
    const float cy = static_cast<float>((docPos.y() - l->offset.y()) / lsy);
    const float radius = brushRadiusLayer();
    if (radius <= 0.0f) return;
    const float strength = dabStrength(false);
    if (strength <= 0.0f) return;
    const float ang = static_cast<float>(std::atan2(lastDragDir_.y(), lastDragDir_.x()));
    const bool alt = QApplication::keyboardModifiers().testFlag(Qt::AltModifier);
    if (state_->liquifyMirrorDab(cx, cy, radius, ang, alt, strength, falloffExp())) {
        strokeMoved_ = true;
        sessionMoved_ = true;
        mirrorAxis_ = lastDragDir_;
        mirrorAxisValid_ = true;
        state_->flushPaint();
        // The mirror dab only touches the disc bbox (same bounds the engine
        // writes): refresh + repaint just that, not the whole layer/widget.
        const QRect dab(static_cast<int>(std::floor(cx - radius)),
                        static_cast<int>(std::floor(cy - radius)),
                        static_cast<int>(std::ceil(2 * radius)) + 2,
                        static_cast<int>(std::ceil(2 * radius)) + 2);
        lastDirtyRect_ = lastDirtyRect_.isNull() ? dab : lastDirtyRect_.united(dab);
        refreshImages();
        const QPointF a = docToView(layerToDoc(dab.topLeft()));
        const QPointF b = docToView(layerToDoc(dab.bottomRight()));
        const double ringR =
            radius * std::max(lsx, lsy) * zoom_ + 8.0;
        const QPointF vc = docToView(docPos);
        update(QRectF(a, b)
                   .normalized()
                   .united(QRectF(vc.x() - ringR, vc.y() - ringR, 2 * ringR,
                                  2 * ringR))
                   .toAlignedRect());
        emit statusChanged();
    }
}

void LiquifyCanvas::applyMaskDab(QPointF docPos, bool freeze) {
    LayerItem *l = state_->activeLayer();
    if (!l || !l->pixels) return;
    if (!ensureSession()) return;
    const double lsx = std::max(l->scaleX, 1e-6);
    const double lsy = std::max(l->scaleY, 1e-6);
    const float cx = static_cast<float>((docPos.x() - l->offset.x()) / lsx);
    const float cy = static_cast<float>((docPos.y() - l->offset.y()) / lsy);
    const float radius = brushRadiusLayer();
    if (radius <= 0.0f) return;
    state_->paintLiquifyMask(cx, cy, radius, brush_.hardness / 100.0f, freeze,
                             dabStrength(false));
    bumpMaskVersion();
    update();
}
void LiquifyCanvas::finishStroke(bool moved) {
    pressed_ = false;
    holdTimer_.stop();
    draggingStroke_ = false;
    if (!moved) {
        if (!undoStack_.empty()) undoStack_.pop_back();
    }
    update();
}

void LiquifyCanvas::updateHoldTimer() {
    if (pressed_ && isRadialTool(brush_.tool) && brush_.tool != 11)
        holdTimer_.start();
    else
        holdTimer_.stop();
}

void LiquifyCanvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    setFocus(Qt::MouseFocusReason);
    DocumentItem *d = state_->activeDocument();
    LayerItem *l = state_->activeLayer();
    if (!d || !l || !l->pixels) return;
    const QPointF docPos = viewToDoc(event->position());
    pressView_ = event->pos();
    pressDoc_ = docPos;
    lastDoc_ = docPos;
    cursorView_ = event->position();
    const bool alt = event->modifiers().testFlag(Qt::AltModifier);
    if (brush_.tool == 14 || spaceHeld_) {
        panning_ = true;
        pressed_ = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (brush_.tool == 15) {
        const double f = alt ? 0.8 : 1.25;
        setZoom(zoom_ * f, event->position());
        emit statusChanged();
        return;
    }
    if (brush_.tool == 11 && alt) {
        if (!ensureSession()) return;
        state_->liquifyCloneSource(float(docPos.x()), float(docPos.y()));
        state_->setStatusHint(tr("Clone source set."));
        update();
        return;
    }
    if (!ensureSession()) return;
    pushStrokeUndoPoint();
    pressed_ = true;
    draggingStroke_ = true;
    strokeMoved_ = false;
    if (brush_.tool == 12 || brush_.tool == 13) {
        applyMaskDab(docPos, brush_.tool == 12);
    } else if (brush_.tool == 9) {
        lastDragDir_ = QPointF(1, 0);
        mirrorAxisValid_ = false;
    } else if (!isDragTool(brush_.tool)) {
        applyDab(docPos, 0.0f, 0.0f);
    }
    updateHoldTimer();
    update();
}

void LiquifyCanvas::mouseMoveEvent(QMouseEvent *event) {
    cursorView_ = event->position();
    DocumentItem *d = state_->activeDocument();
    LayerItem *l = state_->activeLayer();
    if (!d || !l || !l->pixels) return;
    const QPointF docPos = viewToDoc(event->position());
    if (panning_ && pressed_) {
        pan_ += event->position() - pressView_;
        pressView_ = event->pos();
        update();
        return;
    }
    if (!pressed_) {
        update();
        return;
    }
    const double lsx = std::max(l->scaleX, 1e-6);
    const double lsy = std::max(l->scaleY, 1e-6);
    const QPointF delta = docPos - lastDoc_;
    const float dx = static_cast<float>(delta.x() / lsx);
    const float dy = static_cast<float>(delta.y() / lsy);
    if (std::hypot(dx, dy) >= 0.5f) {
        lastDragDir_ = QPointF(delta.x(), delta.y());
        lastDragDir_ /= std::max(1e-9, std::hypot(lastDragDir_.x(), lastDragDir_.y()));
    }
    if (brush_.tool == 12 || brush_.tool == 13) {
        applyMaskDab(docPos, brush_.tool == 12);
    } else if (brush_.tool == 9) {
        if (std::hypot(dx, dy) >= 1.0f) applyMirrorDab(docPos);
    } else if (isDragTool(brush_.tool)) {
        if (std::hypot(dx, dy) >= 1.0f) applyDab(docPos, dx, dy);
    } else if (!isRadialTool(brush_.tool)) {
        applyDab(docPos, dx, dy);
    } else {
        applyDab(docPos, 0.0f, 0.0f);
    }
    lastDoc_ = docPos;
    update();
}

void LiquifyCanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    if (panning_) {
        panning_ = false;
        pressed_ = false;
        unsetCursor();
        return;
    }
    finishStroke(strokeMoved_);
}

void LiquifyCanvas::wheelEvent(QWheelEvent *event) {
    const double f = std::pow(1.0015, event->angleDelta().y());
    setZoom(zoom_ * f, event->position());
    emit statusChanged();
    event->accept();
}

void LiquifyCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !spaceHeld_) {
        spaceHeld_ = true;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    if (event->key() == Qt::Key_BracketLeft || event->key() == Qt::Key_BracketRight) {
        const double f = event->key() == Qt::Key_BracketLeft ? 0.9 : 1.0 / 0.9;
        brush_.size = std::clamp(static_cast<int>(std::round(brush_.size * f)), 1, 1500);
        emit brushSizeChanged(brush_.size);
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void LiquifyCanvas::tabletEvent(QTabletEvent *event) {
    if (!brush_.stylus) {
        event->ignore();
        return;
    }
    tabletPressure_ = std::clamp(event->pressure(), 0.05, 1.0);
    QMouseEvent mouseEvent(event->type() == QEvent::TabletPress ? QEvent::MouseButtonPress
        : event->type() == QEvent::TabletRelease ? QEvent::MouseButtonRelease
                                                 : QEvent::MouseMove,
        event->position(), event->globalPosition(), Qt::LeftButton,
        event->buttons(), event->modifiers());
    event->accept();
    if (event->type() == QEvent::TabletPress)
        mousePressEvent(&mouseEvent);
    else if (event->type() == QEvent::TabletRelease)
        mouseReleaseEvent(&mouseEvent);
    else
        mouseMoveEvent(&mouseEvent);
}

void LiquifyCanvas::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update();
}

void LiquifyCanvas::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    zoomToFit();
    setFocus(Qt::PopupFocusReason);
}

void LiquifyCanvas::captureBackdrop() {
    DocumentItem *d = state_->activeDocument();
    LayerItem *l = state_->activeLayer();
    if (!d || !l || !l->pixels) return;
    backdropComposite_ = d->composite.copy();
    if (auto src = state_->liquifySource())
        frozenLayer_ = imageFromLayerPixels(*src);
    else
        frozenLayer_ = imageFromLayerPixels(*l->pixels);
    refreshBackdrop();
}

void LiquifyCanvas::refreshBackdrop() {
    backdrop_ = view_.backdropSource == 0 ? backdropComposite_ : frozenLayer_;
    update();
}

void LiquifyCanvas::drawChecker(QPainter &p, QRectF r) const {
    const int cell = std::max(4, static_cast<int>(8 * zoom_));
    p.save();
    p.setClipRect(r);
    p.fillRect(r, QColor(128, 128, 128));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(160, 160, 160));
    const int x0 = static_cast<int>(r.left()) / cell * cell;
    const int y0 = static_cast<int>(r.top()) / cell * cell;
    for (int y = y0; y < r.bottom(); y += cell) {
        for (int x = x0; x < r.right(); x += cell) {
            if (((x / cell) + (y / cell)) % 2 == 0)
                p.drawRect(x, y, cell, cell);
        }
    }
    p.restore();
}

QImage LiquifyCanvas::layerImage() const {
    return layerCache_;
}

void LiquifyCanvas::drawMesh(QPainter &p) const {
    if (!state_->liquifySessionActive()) return;
    LayerItem *l = state_->activeLayer();
    if (!l || !l->pixels) return;
    const pittore::compute::WarpMesh &mesh = state_->liquifySessionMesh();
    if (mesh.cols < 2 || mesh.rows < 2) return;
    const int sizeFactor[3] = {1, 2, 4};
    const int sf = sizeFactor[std::clamp(view_.meshSize, 0, 2)];
    const int stride = std::max(1, static_cast<int>(std::round(view_.divisions / 4.0)) * sf);
    QColor col = view_.meshColor;
    col.setAlpha(std::clamp(view_.meshOpacity, 0, 100) * 255 / 100);
    p.save();
    p.setPen(QPen(col, 1));
    const auto displaced = [&](int c, int r) {
        const std::size_t i = (static_cast<std::size_t>(r) * mesh.cols + c) * 2;
        const float vx = mesh.left + c * pittore::compute::kWarpCell + mesh.offsets[i];
        const float vy = mesh.top + r * pittore::compute::kWarpCell + mesh.offsets[i + 1];
        return docToView(layerToDoc(QPointF(vx, vy)));
    };
    for (std::uint32_t r = 0; r < mesh.rows; r += stride) {
        QPainterPath path;
        bool started = false;
        for (std::uint32_t c = 0; c < mesh.cols; c += 1) {
            const QPointF pt = displaced(c, r);
            if (!started) {
                path.moveTo(pt);
                started = true;
            } else {
                path.lineTo(pt);
            }
        }
        p.drawPath(path);
    }
    for (std::uint32_t c = 0; c < mesh.cols; c += stride) {
        QPainterPath path;
        bool started = false;
        for (std::uint32_t r = 0; r < mesh.rows; r += 1) {
            const QPointF pt = displaced(c, r);
            if (!started) {
                path.moveTo(pt);
                started = true;
            } else {
                path.lineTo(pt);
            }
        }
        p.drawPath(path);
    }
    p.restore();
}

void LiquifyCanvas::drawMask(QPainter &p) const {
    LayerItem *l = state_->activeLayer();
    if (!l || !l->pixels) return;
    if (maskCache_.isNull() || maskCacheVersion_ != maskVersion_) {
        const int w = int(l->pixels->width());
        const int h = int(l->pixels->height());
        maskCache_ = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        maskCache_.fill(Qt::transparent);
        const QColor mc = view_.maskColor;
        for (int y = 0; y < h; ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(maskCache_.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const float m = state_->liquifyMaskAt(float(x), float(y));
                if (m <= 0.001f) continue;
                const int a = int(m * 110);
                row[x] = qRgba(mc.red() * a / 255, mc.green() * a / 255, mc.blue() * a / 255, a);
            }
        }
        maskCacheVersion_ = maskVersion_;
    }
    const QPointF tl = docToView(layerToDoc(QPointF(0, 0)));
    const double sx = zoom_ * l->scaleX;
    const double sy = zoom_ * l->scaleY;
    p.drawImage(QRectF(tl.x(), tl.y(), maskCache_.width() * sx, maskCache_.height() * sy), maskCache_);
}

void LiquifyCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), bg_);
    DocumentItem *d = state_->activeDocument();
    LayerItem *l = state_->activeLayer();
    if (!d || !l || !l->pixels) return;
    const QRectF docRect(0, 0, double(d->size.width()), double(d->size.height()));
    const QRectF viewRect(docToView(docRect.topLeft()), docToView(docRect.bottomRight()));
    if (view_.showBackdrop && !backdrop_.isNull()) {
        p.save();
        p.setOpacity(view_.backdropOpacity / 100.0);
        if (view_.backdropMode == 0) {
            p.drawImage(viewRect, backdrop_);
        }
        p.restore();
    }
    if (view_.showImage && !layerCache_.isNull()) {
        const QPointF tl = docToView(layerToDoc(QPointF(0, 0)));
        const double sx = zoom_ * l->scaleX;
        const double sy = zoom_ * l->scaleY;
        QRectF lr(tl.x(), tl.y(), layerCache_.width() * sx, layerCache_.height() * sy);
        drawChecker(p, lr);
        p.drawImage(lr, layerCache_);
    } else if (!view_.showImage) {
        drawChecker(p, viewRect);
    }
    if (view_.showBackdrop && !backdrop_.isNull() && view_.backdropMode == 1) {
        p.save();
        p.setOpacity(view_.backdropOpacity / 100.0);
        p.drawImage(viewRect, backdrop_);
        p.restore();
    }
    if (view_.showMask && state_->liquifyMaskPresent()) drawMask(p);
    if (view_.showMesh) drawMesh(p);
    if (brush_.tool == 11 && state_->liquifyHasCloneSource()) {
        const QPointF sp = docToView(layerToDoc(QPointF(
            (state_->liquifyCloneSourcePoint().x() - l->offset.x()) / std::max(l->scaleX, 1e-6),
            (state_->liquifyCloneSourcePoint().y() - l->offset.y()) / std::max(l->scaleY, 1e-6))));
        p.save();
        p.setPen(QPen(Qt::white, 1));
        p.drawLine(sp + QPointF(-8, 0), sp + QPointF(8, 0));
        p.drawLine(sp + QPointF(0, -8), sp + QPointF(0, 8));
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(sp, 10, 10);
        p.restore();
    }
    if (brush_.tool == 9 && mirrorAxisValid_) {
        const QPointF c = docToView(lastDoc_);
        const QPointF dir(mirrorAxis_.x() * l->scaleX * zoom_, mirrorAxis_.y() * l->scaleY * zoom_);
        const double len = std::max(1.0, std::hypot(dir.x(), dir.y()));
        const QPointF u = dir / len * 40.0;
        p.save();
        QPen dashed(Qt::white, 1, Qt::DashLine);
        p.setPen(dashed);
        p.drawLine(c - u, c + u);
        p.restore();
    }
    if (cursorView_.x() >= 0 && brush_.tool < 14) {
        const double rad = brush_.size * 0.5 * zoom_;
        p.save();
        p.setPen(QPen(QColor(255, 255, 255, 200), 1));
        p.drawEllipse(cursorView_, rad, rad);
        p.setPen(QPen(QColor(0, 0, 0, 200), 1));
        p.drawEllipse(cursorView_, rad + 1, rad + 1);
        p.restore();
    }
    if (panning_) setCursor(Qt::ClosedHandCursor);
    else if (spaceHeld_ || brush_.tool == 14) setCursor(Qt::OpenHandCursor);
    else if (brush_.tool == 15) setCursor(Qt::CrossCursor);
    else unsetCursor();
}
LiquifyDialog::LiquifyDialog(AppState *state, QWidget *parent)
    : QDialog(parent), state_(state) {
    setWindowTitle(tr("Liquify"));
    setModal(true);
    resize(1180, 760);
    canvas_ = new LiquifyCanvas(state_, this);
    buildToolbar();
    buildProperties();
    buildBottomBar();
    auto *middle = new QSplitter(Qt::Horizontal, this);
    middle->addWidget(toolbar_);
    middle->addWidget(canvas_);
    auto *sideScroll = new QScrollArea(this);
    sideScroll->setWidget(props_);
    sideScroll->setWidgetResizable(true);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideScroll->setFixedWidth(360);
    sideScroll->setFrameShape(QFrame::NoFrame);
    middle->addWidget(sideScroll);
    middle->setSizes({44, 740, 360});
    middle->setStretchFactor(0, 0);
    middle->setStretchFactor(1, 1);
    middle->setStretchFactor(2, 0);
    auto *root = new QVBoxLayout(this);
    root->addWidget(middle, 1);
    root->addWidget(bottom_);
    DocumentItem *d = state_->activeDocument();
    LayerItem *l = state_->activeLayer();
    if (d && l && l->pixels) {
        canvas_->captureBackdrop();
    }
    canvas_->refreshImages();
    canvas_->zoomToFit();
    setTool(0);
    updateZoomLabel();
    connect(canvas_, &LiquifyCanvas::brushSizeChanged, this,
            [this](int s) { syncSizeWidgets(s); });
    connect(canvas_, &LiquifyCanvas::statusChanged, this,
            [this] { updateZoomLabel(); });
    struct ShortcutDef {
        const char *key;
        int tool;
    };
    const ShortcutDef keys[] = {{"W", 0}, {"R", 1}, {"E", 2}, {"C", 3}, {"S", 5},
                                {"B", 6}, {"O", 7}, {"M", 9}, {"T", 10}, {"K", 11},
                                {"F", 12}, {"D", 13}, {"H", 14}, {"Z", 15}};
    for (const auto &k : keys) {
        auto *sc = new QShortcut(QKeySequence(QString::fromLatin1(k.key)), this);
        connect(sc, &QShortcut::activated, this, [this, k] { setTool(k.tool); });
    }
    auto *undoSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Z")), this);
    connect(undoSc, &QShortcut::activated, this, [this] {
        if (canvas_->undoStroke()) updateZoomLabel();
    });
    connect(state_, &AppState::themeChanged, this,
            [this] { applyDialogTheme(); });
}

LiquifyDialog::~LiquifyDialog() = default;

void LiquifyDialog::buildToolbar() {
    toolbar_ = new QToolBar(this);
    toolbar_->setOrientation(Qt::Vertical);
    toolbar_->setFloatable(false);
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(22, 22));
    toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_->setFixedWidth(40);
    int count = 0;
    const LiquifyToolDef *defs = liquifyTools(count);
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < count; ++i) {
        if (i == 12 || i == 14) toolbar_->addSeparator();
        QAction *a = toolbar_->addAction(QString());
        a->setCheckable(true);
        a->setToolTip(QString::fromLatin1(defs[i].name) +
                      QStringLiteral(" (") +
                      QString::fromLatin1(defs[i].shortcut) + QStringLiteral(")"));
        a->setStatusTip(a->toolTip());
        group->addAction(a);
        toolActions_.push_back(a);
        const int tool = defs[i].mode;
        connect(a, &QAction::triggered, this, [this, tool] { setTool(tool); });
    }
    applyDialogTheme();
}

void LiquifyDialog::applyDialogTheme() {
    static const char *keys[16] = {
        "lq-forward", "lq-reconstruct", "lq-smooth", "lq-twirl-cw",
        "lq-twirl-ccw", "lq-pucker", "lq-bloat", "lq-push-left",
        "lq-push-right", "lq-mirror", "lq-turbulence", "lq-clone",
        "lq-freeze", "lq-thaw", "hand", "zoom",
    };
    const ThemeColors c = colorsFor(state_->theme());
    for (std::size_t i = 0; i < toolActions_.size() && i < 16; ++i)
        toolActions_[i]->setIcon(
            chromeIcon(QString::fromLatin1(keys[i]), c.text, c.accentText));
}

class LiquifySection final : public QWidget {
public:
    LiquifySection(const QString &title, QWidget *parent = nullptr)
        : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        header_ = new QToolButton(this);
        header_->setText(title);
        header_->setCheckable(true);
        header_->setChecked(true);
        header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header_->setArrowType(Qt::DownArrow);
        header_->setAutoRaise(true);
        header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QFont f = header_->font();
        f.setBold(true);
        header_->setFont(f);
        body_ = new QWidget(this);
        layout->addWidget(header_);
        layout->addWidget(body_);
        connect(header_, &QToolButton::toggled, this, [this](bool on) {
            body_->setVisible(on);
            header_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        });
    }

    QVBoxLayout *bodyLayout() {
        if (!bodyLayout_) {
            bodyLayout_ = new QVBoxLayout(body_);
            bodyLayout_->setContentsMargins(8, 4, 8, 12);
            bodyLayout_->setSpacing(6);
        }
        return bodyLayout_;
    }

private:
    QToolButton *header_ = nullptr;
    QWidget *body_ = nullptr;
    QVBoxLayout *bodyLayout_ = nullptr;
};

void LiquifyDialog::buildProperties() {
    props_ = new QWidget(this);
    auto *propsLayout = new QVBoxLayout(props_);
    propsLayout->setContentsMargins(0, 0, 0, 0);
    propsLayout->setSpacing(6);
    auto &brush = canvas_->brush();
    auto &view = canvas_->view();
    {
        auto *section0 = new LiquifySection(tr("Brush Tool Options"), props_);
        QVBoxLayout *form0 = section0->bodyLayout();
        form0->addWidget(makeSliderRow(tr("Size"), 1, 1500, brush.size, tr(" px"),
                                      [this](int v) {
                                          canvas_->brush().size = v;
                                          canvas_->update();
                                      },
                                      &sizeSpin_));
        form0->addWidget(makeSliderRow(tr("Density"), 0, 100, brush.density, tr(" %"),
                                      [this](int v) { canvas_->brush().density = v; }));
        form0->addWidget(makeSliderRow(tr("Pressure"), 1, 100, brush.pressure, tr(" %"),
                                      [this](int v) { canvas_->brush().pressure = v; }));
        form0->addWidget(makeSliderRow(tr("Rate"), 0, 100, brush.rate, QString(),
                                      [this](int v) { canvas_->brush().rate = v; }));
        form0->addWidget(makeSliderRow(tr("Turbulent Jitter"), 0, 100, brush.jitter, QString(),
                                      [this](int v) { canvas_->brush().jitter = v; }));
        form0->addWidget(makeSliderRow(tr("Opacity"), 1, 100, brush.opacity, tr(" %"),
                                      [this](int v) { canvas_->brush().opacity = v; }));
        form0->addWidget(makeSliderRow(tr("Speed"), 1, 100, brush.speed, tr(" %"),
                                      [this](int v) { canvas_->brush().speed = v; }));
        form0->addWidget(makeSliderRow(tr("Hardness"), 0, 100, brush.hardness, tr(" %"),
                                      [this](int v) { canvas_->brush().hardness = v; }));
        auto *rampRow = new QHBoxLayout;
        auto *rampLabel = new QLabel(tr("Ramp"));
        rampLabel->setMinimumWidth(84);
        rampCombo_ = new QComboBox;
        rampCombo_->addItems({tr("Gaussian"), tr("Linear"), tr("Smooth")});
        rampCombo_->setCurrentIndex(brush.ramp);
        connect(rampCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int i) { canvas_->brush().ramp = i; });
        rampRow->addWidget(rampLabel);
        rampRow->addWidget(rampCombo_, 1);
        auto *rampHost = new QWidget;
        rampHost->setLayout(rampRow);
        rampRow->setContentsMargins(0, 0, 0, 0);
        form0->addWidget(rampHost);
        stylusBox_ = new QCheckBox(tr("Stylus Pressure"));
        stylusBox_->setChecked(brush.stylus);
        connect(stylusBox_, &QCheckBox::toggled, this,
                [this](bool on) { canvas_->brush().stylus = on; });
        form0->addWidget(stylusBox_);
        pinBox_ = new QCheckBox(tr("Pin Edges"));
        pinBox_->setChecked(brush.pinEdges);
        connect(pinBox_, &QCheckBox::toggled, this,
                [this](bool on) { canvas_->brush().pinEdges = on; });
        form0->addWidget(pinBox_);
        propsLayout->addWidget(section0);
    }
    {
        auto *section1 = new LiquifySection(tr("Reconstruct Options"), props_);
        QVBoxLayout *form1 = section1->bodyLayout();
        auto *modeRow = new QHBoxLayout;
        auto *modeLabel = new QLabel(tr("Reconstruct"));
        modeLabel->setMinimumWidth(84);
        reconCombo_ = new QComboBox;
        reconCombo_->addItems(
            {tr("Revert"), tr("Rigid"), tr("Stiff"), tr("Loose")});
        reconCombo_->setCurrentIndex(brush.reconstructMode);
        connect(reconCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int i) { canvas_->brush().reconstructMode = i; });
        modeRow->addWidget(modeLabel);
        modeRow->addWidget(reconCombo_, 1);
        auto *modeHost = new QWidget;
        modeHost->setLayout(modeRow);
        modeRow->setContentsMargins(0, 0, 0, 0);
        form1->addWidget(modeHost);
        auto *reconstructBtn = new QPushButton(tr("Reconstruct"));
        connect(reconstructBtn, &QPushButton::clicked, this,
                [this] { reconstructAll(); });
        form1->addWidget(reconstructBtn);
        auto *restoreBtn = new QPushButton(tr("Restore All"));
        connect(restoreBtn, &QPushButton::clicked, this,
                [this] { resetMesh(); });
        form1->addWidget(restoreBtn);
        propsLayout->addWidget(section1);
    }
    {
        auto *section2 = new LiquifySection(tr("Mask Options"), props_);
        QVBoxLayout *form2 = section2->bodyLayout();
        auto *srcRow = new QHBoxLayout;
        auto *srcLabel = new QLabel(tr("Source"));
        srcLabel->setMinimumWidth(84);
        maskSourceCombo_ = new QComboBox;
        maskSourceCombo_->addItems(
            {tr("Selection"), tr("Layer Mask"), tr("Transparency")});
        srcRow->addWidget(srcLabel);
        srcRow->addWidget(maskSourceCombo_, 1);
        auto *srcHost = new QWidget;
        srcHost->setLayout(srcRow);
        srcRow->setContentsMargins(0, 0, 0, 0);
        form2->addWidget(srcHost);
        auto *opRow = new QHBoxLayout;
        opRow->setSpacing(8);
        const QString ops[5] = {tr("Replace"), tr("Add"), tr("Subtract"),
                                tr("Intersect"), tr("Invert")};
        for (int i = 0; i < 5; ++i) {
            auto *b = new QPushButton(ops[i]);
            b->setStyleSheet(QStringLiteral("QPushButton { padding: 5px 3px; }"));
            connect(b, &QPushButton::clicked, this, [this, i] { applyMaskSource(i); });
            opRow->addWidget(b);
        }
        auto *opHost = new QWidget;
        opHost->setLayout(opRow);
        opRow->setContentsMargins(0, 0, 0, 0);
        form2->addWidget(opHost);
        auto *row2 = new QHBoxLayout;
        row2->setSpacing(8);
        const QString ops2[3] = {tr("None"), tr("Mask All"), tr("Invert All")};
        for (int i = 0; i < 3; ++i) {
            auto *b = new QPushButton(ops2[i]);
            b->setStyleSheet(QStringLiteral("QPushButton { padding: 5px 3px; }"));
            connect(b, &QPushButton::clicked, this, [this, i] {
                if (i == 0) state_->clearLiquifyMask();
                else if (i == 1) state_->fillLiquifyMask(1.0f);
                else state_->invertLiquifyMask();
                canvas_->bumpMaskVersion();
                canvas_->update();
            });
            row2->addWidget(b);
        }
        auto *row2Host = new QWidget;
        row2Host->setLayout(row2);
        row2->setContentsMargins(0, 0, 0, 0);
        form2->addWidget(row2Host);
        propsLayout->addWidget(section2);
    }
    {
        auto *section3 = new LiquifySection(tr("View Options"), props_);
        QVBoxLayout *form3 = section3->bodyLayout();
        showImageBox_ = new QCheckBox(tr("Show Image"));
        showImageBox_->setChecked(view.showImage);
        connect(showImageBox_, &QCheckBox::toggled, this, [this](bool on) {
            canvas_->view().showImage = on;
            canvas_->update();
        });
        form3->addWidget(showImageBox_);
        showMeshBox_ = new QCheckBox(tr("Show Mesh"));
        showMeshBox_->setChecked(view.showMesh);
        connect(showMeshBox_, &QCheckBox::toggled, this, [this](bool on) {
            canvas_->view().showMesh = on;
            canvas_->update();
        });
        form3->addWidget(showMeshBox_);
        auto *sizeRow = new QHBoxLayout;
        auto *sizeLabel = new QLabel(tr("Mesh Size"));
        sizeLabel->setMinimumWidth(84);
        meshSizeCombo_ = new QComboBox;
        meshSizeCombo_->addItems({tr("Small"), tr("Medium"), tr("Large")});
        meshSizeCombo_->setCurrentIndex(view.meshSize);
        connect(meshSizeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int i) {
                    canvas_->view().meshSize = i;
                    canvas_->update();
                });
        sizeRow->addWidget(sizeLabel);
        sizeRow->addWidget(meshSizeCombo_, 1);
        auto *sizeHost = new QWidget;
        sizeHost->setLayout(sizeRow);
        sizeRow->setContentsMargins(0, 0, 0, 0);
        form3->addWidget(sizeHost);
        auto *colorRow = new QHBoxLayout;
        auto *colorLabel = new QLabel(tr("Mesh Color"));
        colorLabel->setMinimumWidth(84);
        meshColorCombo_ = colorCombo(view.meshColor);
        connect(meshColorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] {
                    canvas_->view().meshColor =
                        colorByName(meshColorCombo_->currentText(), QColor(0, 200, 255));
                    canvas_->update();
                });
        colorRow->addWidget(colorLabel);
        colorRow->addWidget(meshColorCombo_, 1);
        auto *colorHost = new QWidget;
        colorHost->setLayout(colorRow);
        colorRow->setContentsMargins(0, 0, 0, 0);
        form3->addWidget(colorHost);
        showMaskBox_ = new QCheckBox(tr("Show Mask"));
        showMaskBox_->setChecked(view.showMask);
        connect(showMaskBox_, &QCheckBox::toggled, this, [this](bool on) {
            canvas_->view().showMask = on;
            canvas_->update();
        });
        form3->addWidget(showMaskBox_);
        auto *maskColorRow = new QHBoxLayout;
        auto *maskColorLabel = new QLabel(tr("Mask Color"));
        maskColorLabel->setMinimumWidth(84);
        maskColorCombo_ = colorCombo(view.maskColor);
        connect(maskColorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] {
                    canvas_->view().maskColor =
                        colorByName(maskColorCombo_->currentText(), QColor(255, 0, 0));
                    canvas_->bumpMaskVersion();
                    canvas_->update();
                });
        maskColorRow->addWidget(maskColorLabel);
        maskColorRow->addWidget(maskColorCombo_, 1);
        auto *maskColorHost = new QWidget;
        maskColorHost->setLayout(maskColorRow);
        maskColorRow->setContentsMargins(0, 0, 0, 0);
        form3->addWidget(maskColorHost);
        showBackdropBox_ = new QCheckBox(tr("Show Backdrop"));
        showBackdropBox_->setChecked(view.showBackdrop);
        connect(showBackdropBox_, &QCheckBox::toggled, this, [this](bool on) {
            canvas_->view().showBackdrop = on;
            canvas_->update();
        });
        form3->addWidget(showBackdropBox_);
        auto *useRow = new QHBoxLayout;
        auto *useLabel = new QLabel(tr("Use"));
        useLabel->setMinimumWidth(84);
        backdropUseCombo_ = new QComboBox;
        backdropUseCombo_->addItems({tr("All Layers"), tr("Active Layer Only")});
        backdropUseCombo_->setCurrentIndex(view.backdropSource);
        connect(backdropUseCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int i) {
                    canvas_->view().backdropSource = i;
                    canvas_->refreshBackdrop();
                    canvas_->update();
                });
        useRow->addWidget(useLabel);
        useRow->addWidget(backdropUseCombo_, 1);
        auto *useHost = new QWidget;
        useHost->setLayout(useRow);
        useRow->setContentsMargins(0, 0, 0, 0);
        form3->addWidget(useHost);
        auto *modeRow = new QHBoxLayout;
        auto *modeLabel = new QLabel(tr("Mode"));
        modeLabel->setMinimumWidth(84);
        backdropModeCombo_ = new QComboBox;
        backdropModeCombo_->addItems({tr("Behind"), tr("In Front")});
        backdropModeCombo_->setCurrentIndex(view.backdropMode);
        connect(backdropModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int i) {
                    canvas_->view().backdropMode = i;
                    canvas_->update();
                });
        modeRow->addWidget(modeLabel);
        modeRow->addWidget(backdropModeCombo_, 1);
        auto *modeHost = new QWidget;
        modeHost->setLayout(modeRow);
        modeRow->setContentsMargins(0, 0, 0, 0);
        form3->addWidget(modeHost);
        form3->addWidget(makeSliderRow(tr("Opacity"), 0, 100, view.backdropOpacity, tr(" %"),
                                      [this](int v) {
                                          canvas_->view().backdropOpacity = v;
                                          canvas_->update();
                                      }));
        propsLayout->addWidget(section3);
    }
    {
        auto *section4 = new LiquifySection(tr("Mesh Options"), props_);
        QVBoxLayout *form4 = section4->bodyLayout();
        form4->addWidget(makeSliderRow(tr("Divisions"), 4, 128, view.divisions, tr(" px"),
                                      [this](int v) {
                                          canvas_->view().divisions = v;
                                          canvas_->update();
                                      }));
        form4->addWidget(makeSliderRow(tr("Mesh Opacity"), 0, 100, view.meshOpacity, tr(" %"),
                                      [this](int v) {
                                          canvas_->view().meshOpacity = v;
                                          canvas_->update();
                                      }));
        form4->addWidget(makeSliderRow(tr("Reconstruct Mesh"), 0, 100, 100, tr(" %"),
                                      [this](int) {}, &reconStrengthSpin_));
        auto *applyBtn = new QPushButton(tr("Apply"));
        connect(applyBtn, &QPushButton::clicked, this, [this] { applyMeshStrength(); });
        form4->addWidget(applyBtn);
        auto *loadBtn = new QPushButton(tr("Load Mesh…"));
        connect(loadBtn, &QPushButton::clicked, this, [this] { loadMesh(false); });
        form4->addWidget(loadBtn);
        auto *lastBtn = new QPushButton(tr("Load Last Mesh"));
        connect(lastBtn, &QPushButton::clicked, this, [this] { loadMesh(true); });
        form4->addWidget(lastBtn);
        auto *saveBtn = new QPushButton(tr("Save Mesh…"));
        connect(saveBtn, &QPushButton::clicked, this, [this] { saveMesh(); });
        form4->addWidget(saveBtn);
        auto *resetBtn = new QPushButton(tr("Reset Mesh"));
        connect(resetBtn, &QPushButton::clicked, this, [this] { resetMesh(); });
        form4->addWidget(resetBtn);
        propsLayout->addWidget(section4);
        propsLayout->addStretch(1);
    }
}
QWidget *LiquifyDialog::makeSliderRow(const QString &label, int lo, int hi,
                                      int value, const QString &suffix,
                                      std::function<void(int)> onChange,
                                      QSpinBox **outSpin) {
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *name = new QLabel(label);
    name->setMinimumWidth(84);
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(lo, hi);
    slider->setValue(value);
    auto *spin = new QSpinBox(row);
    spin->setRange(lo, hi);
    spin->setValue(value);
    if (!suffix.isEmpty()) spin->setSuffix(suffix);
    spin->setFixedWidth(78);
    connect(slider, &QSlider::valueChanged, this,
            [spin, onChange](int v) {
                QSignalBlocker block(spin);
                spin->setValue(v);
                onChange(v);
            });
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
            [slider, onChange](int v) {
                QSignalBlocker block(slider);
                slider->setValue(v);
                onChange(v);
            });
    layout->addWidget(name);
    layout->addWidget(slider, 1);
    layout->addWidget(spin);
    if (outSpin) *outSpin = spin;
    return row;
}

QComboBox *LiquifyDialog::colorCombo(QColor current) {
    auto *combo = new QComboBox;
    const QString names[9] = {tr("Red"), tr("Green"), tr("Blue"), tr("Cyan"),
                              tr("Yellow"), tr("Magenta"), tr("Grey"), tr("Black"), tr("White")};
    const QColor cols[9] = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 120, 255),
                            QColor(0, 200, 255), QColor(255, 255, 0), QColor(255, 0, 255),
                            QColor(160, 160, 160), QColor(0, 0, 0), QColor(255, 255, 255)};
    int pick = 0;
    int best = INT_MAX;
    for (int i = 0; i < 9; ++i) {
        combo->addItem(names[i]);
        const int d = std::abs(cols[i].red() - current.red()) +
                      std::abs(cols[i].green() - current.green()) +
                      std::abs(cols[i].blue() - current.blue());
        if (d < best) {
            best = d;
            pick = i;
        }
    }
    combo->setCurrentIndex(pick);
    return combo;
}

void LiquifyDialog::buildBottomBar() {
    bottom_ = new QWidget(this);
    auto *layout = new QHBoxLayout(bottom_);
    layout->setContentsMargins(0, 4, 0, 0);
    zoomLabel_ = new QLabel(QStringLiteral("100%"));
    zoomCombo_ = new QComboBox;
    zoomCombo_->setObjectName(QStringLiteral("liquifyZoomCombo"));
    zoomCombo_->addItems({QStringLiteral("25%"), QStringLiteral("50%"),
                          QStringLiteral("100%"), QStringLiteral("200%"),
                          tr("Fit")});
    zoomCombo_->setCurrentIndex(4);
    connect(zoomCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) {
                if (i == 4) {
                    canvas_->zoomToFit();
                } else {
                    const double z[4] = {0.25, 0.5, 1.0, 2.0};
                    canvas_->setZoom(z[i]);
                }
                updateZoomLabel();
            });
    layout->addWidget(zoomLabel_);
    layout->addWidget(zoomCombo_);
    layout->addStretch(1);
    previewBox_ = new QCheckBox(tr("Preview"));
    previewBox_->setChecked(true);
    connect(previewBox_, &QCheckBox::toggled, this, [this] { updatePreview(); });
    layout->addWidget(previewBox_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { reject(); });
    layout->addWidget(buttons);
}

void LiquifyDialog::setTool(int tool) {
    currentTool_ = tool;
    canvas_->brush().tool = tool;
    for (std::size_t i = 0; i < toolActions_.size(); ++i)
        toolActions_[i]->setChecked(static_cast<int>(i) == tool);
    canvas_->update();
}

void LiquifyDialog::syncSizeWidgets(int size) {
    if (sizeSpin_) {
        QSignalBlocker block(sizeSpin_);
        sizeSpin_->setValue(size);
    }
    updateZoomLabel();
}

void LiquifyDialog::updateZoomLabel() {
    if (zoomLabel_)
        zoomLabel_->setText(QStringLiteral("%1%").arg(canvas_->zoom() * 100.0, 0, 'f', 1));
    LayerItem *l = state_->activeLayer();
    const QString name = l ? l->name : QString();
    if (!name.isEmpty())
        setWindowTitle(tr("Liquify (%1 @ %2%)").arg(name).arg(canvas_->zoom() * 100.0, 0, 'f', 1));
    else
        setWindowTitle(tr("Liquify"));
}

std::vector<float> LiquifyDialog::buildMaskSource() {
    std::vector<float> out;
    DocumentItem *d = state_->activeDocument();
    LayerItem *l = state_->activeLayer();
    if (!d || !l || !l->pixels) return out;
    const std::uint32_t w = l->pixels->width();
    const std::uint32_t h = l->pixels->height();
    out.assign(static_cast<std::size_t>(w) * h, 0.0f);
    const int src = maskSourceCombo_ ? maskSourceCombo_->currentIndex() : 0;
    const double lsx = std::max(l->scaleX, 1e-6);
    const double lsy = std::max(l->scaleY, 1e-6);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            float v = 0.0f;
            if (src == 0) {
                if (d->selectionIsMask && !d->selectionMask.isNull()) {
                    const double dx = l->offset.x() + (x + 0.5) * lsx;
                    const double dy = l->offset.y() + (y + 0.5) * lsy;
                    const int sx = std::clamp(static_cast<int>(dx), 0, d->selectionMask.width() - 1);
                    const int sy = std::clamp(static_cast<int>(dy), 0, d->selectionMask.height() - 1);
                    v = qGray(d->selectionMask.pixel(sx, sy)) / 255.0f;
                } else if (!d->selection.isEmpty()) {
                    const double dx = l->offset.x() + (x + 0.5) * lsx;
                    const double dy = l->offset.y() + (y + 0.5) * lsy;
                    v = d->selection.contains(dx, dy) ? 1.0f : 0.0f;
                }
            } else if (src == 1) {
                if (l->hasMask && l->mask) {
                    const double fx = static_cast<double>(x) * l->mask->width() / w;
                    const double fy = static_cast<double>(y) * l->mask->height() / h;
                    const std::uint32_t mx = std::min(static_cast<std::uint32_t>(fx), l->mask->width() - 1);
                    const std::uint32_t my = std::min(static_cast<std::uint32_t>(fy), l->mask->height() - 1);
                    v = l->mask->at(mx, my).r;
                }
            } else {
                v = 1.0f - l->pixels->at(x, y).a;
            }
            out[static_cast<std::size_t>(y) * w + x] = v;
        }
    }
    return out;
}

void LiquifyDialog::applyMaskSource(int op) {
    if (op < 4) {
        std::vector<float> src = buildMaskSource();
        if (src.empty()) {
            state_->setStatusHint(tr("No mask source available."));
            return;
        }
        bool any = false;
        for (float v : src) {
            if (v > 0.001f) {
                any = true;
                break;
            }
        }
        if (!any && op != 4) {
            state_->setStatusHint(tr("Mask source is empty."));
            return;
        }
        canvas_->pushStrokeUndoPoint();
        if (!state_->liquifyMaskCombine(src, op)) {
            state_->setStatusHint(tr("Start a session first."));
            return;
        }
    } else {
        canvas_->pushStrokeUndoPoint();
        state_->invertLiquifyMask();
    }
    canvas_->bumpMaskVersion();
    canvas_->update();
}

void LiquifyDialog::reconstructAll() {
    if (!state_->liquifySessionActive()) {
        state_->setStatusHint(tr("Nothing to reconstruct."));
        return;
    }
    static const float factors[4] = {1.0f, 0.45f, 0.7f, 0.9f};
    const int mode = std::clamp(canvas_->brush().reconstructMode, 0, 3);
    LayerItem *l = state_->activeLayer();
    if (!l || !l->pixels) return;
    const float cx = l->pixels->width() * 0.5f;
    const float cy = l->pixels->height() * 0.5f;
    const float r = std::hypot(float(l->pixels->width()), float(l->pixels->height()));
    canvas_->pushStrokeUndoPoint();
    pittore::compute::warp_mesh_relax(state_->liquifySessionMesh(), cx, cy, r, factors[mode]);
    pittore::compute::warp_mesh_relax(state_->liquifySessionMesh(), cx, cy, r, factors[mode]);
    if (state_->renderLiquifyFull()) {
        canvas_->refreshImages();
        canvas_->update();
    }
}

void LiquifyDialog::resetMesh() {
    state_->resetLiquifyMesh();
    lastMeshStrength_ = 100.0;
    canvas_->refreshImages();
    canvas_->update();
}

void LiquifyDialog::applyMeshStrength() {
    if (!state_->liquifySessionActive()) return;
    const double pct = reconStrengthSpin_ ? reconStrengthSpin_->value() : 100.0;
    if (pct == lastMeshStrength_) return;
    const float f = lastMeshStrength_ > 0 ? static_cast<float>(pct / lastMeshStrength_) : 1.0f;
    lastMeshStrength_ = pct;
    canvas_->pushStrokeUndoPoint();
    state_->scaleLiquifyMesh(f);
    if (state_->renderLiquifyFull()) {
        canvas_->refreshImages();
        canvas_->update();
    }
}

void LiquifyDialog::loadMesh(bool last) {
    QString path = last ? lastMeshPath_
                        : QFileDialog::getOpenFileName(this, tr("Load Mesh"), QString(),
                                                       tr("Liquify Mesh (*.iflq);;All Files (*)"));
    if (path.isEmpty()) return;
    if (state_->loadLiquifyMesh(path)) {
        lastMeshPath_ = path;
        lastMeshStrength_ = 100.0;
        canvas_->bumpMaskVersion();
        canvas_->refreshImages();
        canvas_->update();
    } else {
        state_->setStatusHint(tr("Could not load mesh (size mismatch or bad file)."));
    }
}

void LiquifyDialog::saveMesh() {
    QString path = QFileDialog::getSaveFileName(this, tr("Save Mesh"), lastMeshPath_.isEmpty() ? QStringLiteral("mesh.iflq") : lastMeshPath_,
                                                tr("Liquify Mesh (*.iflq);;All Files (*)"));
    if (path.isEmpty()) return;
    if (state_->saveLiquifyMesh(path)) {
        lastMeshPath_ = path;
    } else {
        state_->setStatusHint(tr("Could not save mesh."));
    }
}

void LiquifyDialog::updatePreview() {
    const bool on = previewBox_ ? previewBox_->isChecked() : true;
    if (on == previewOn_) return;
    previewOn_ = on;
    if (!state_->liquifySessionActive()) return;
    if (!on) {
        previewSaved_ = state_->liquifySessionMesh();
        previewHasSaved_ = true;
        LayerItem *l = state_->activeLayer();
        if (l && l->pixels)
            state_->setLiquifyMesh(pittore::compute::make_warp_mesh(l->pixels->width(), l->pixels->height()));
    } else if (previewHasSaved_) {
        state_->setLiquifyMesh(std::move(previewSaved_));
        previewHasSaved_ = false;
    }
    if (state_->renderLiquifyFull()) {
        canvas_->refreshImages();
        canvas_->update();
    }
}

void LiquifyDialog::accept() {
    if (previewHasSaved_) {
        state_->setLiquifyMesh(std::move(previewSaved_));
        previewHasSaved_ = false;
        state_->renderLiquifyFull();
    }
    const bool moved = canvas_->sessionMoved() || state_->liquifySessionMoved();
    if (!lastMeshPath_.isEmpty()) state_->saveLiquifyMesh(lastMeshPath_);
    if (moved)
        state_->commitUndoStep(tr("Liquify"), QStringLiteral("liquify"));
    else
        state_->discardUndoStep();
    state_->endLiquifySession();
    QDialog::accept();
}

void LiquifyDialog::reject() {
    if (previewHasSaved_) {
        previewHasSaved_ = false;
    }
    state_->cancelLiquifySession();
    QDialog::reject();
}

}
