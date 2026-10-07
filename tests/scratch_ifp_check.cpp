#include <QCoreApplication>
#include <QDir>
#include <cstdio>
#include "engine/compute/layer_mask.h"
#include "engine/compute/tone_blend.h"
#include "engine/core/log.h"
#include "ui/app_state.h"
using namespace pittore;
using namespace pittore::ui;
static void pr(const char* t, const RGBAf* b, int w, int x, int y) {
    const RGBAf& p = b[y*w+x];
    std::printf("%s (%d,%d)=(%.3f,%.3f,%.3f,%.3f)\n", t, x, y, p.r, p.g, p.b, p.a);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::pittore::core::log::set_log_dir("/tmp");
    const QString project =
        argc > 1 ? QString::fromLocal8Bit(argv[1])
                 : QString::fromLocal8Bit(qgetenv("PITTORE_IFP_FIXTURE"));
    if (project.isEmpty()) {
        std::printf("skip: pass a .psc/.ifp path or set PITTORE_IFP_FIXTURE\n");
        return 0;
    }
    AppState state;
    QString err;
    if (!state.openProject(project, &err)) {
        std::printf("open failed\n"); return 2;
    }
    DocumentItem* doc = state.activeDocument();
    const int W = doc->size.width(), H = doc->size.height();
    // Find group + image Test + OHR.
    int gi = -1, ii = -1, oi = -1;
    for (int i = 0; i < doc->layers.size(); ++i) {
        const QString n = doc->layers[i].name;
        if (doc->layers[i].toneBlendGroup) gi = i;
        if (n == QStringLiteral("image Test")) ii = i;
        if (n == QStringLiteral("OHR")) oi = i;
    }
    const auto& tp = doc->layers[gi].toneBlend;
    std::printf("group=%d img=%d ohr=%d params s=%.2f c=%.2f t=%.2f lp=%.2f ct=%d\n",
                gi, ii, oi, tp.strength, tp.color, tp.contrast, tp.lowPass, tp.contentType);
    auto view = [&](const LayerItem& l, int x0, int y0, int x1, int y1) {
        compute::HostPlacedLayer v{};
        v.src = l.pixels->data();
        v.sw = l.pixels->width(); v.sh = l.pixels->height();
        v.ox = l.offset.x(); v.oy = l.offset.y();
        v.sx = l.scaleX; v.sy = l.scaleY;
        v.x0 = x0; v.y0 = y0; v.x1 = x1; v.y1 = y1;
        v.fold = 1.0f; v.mode = compute::BlendMode::Normal;
        return v;
    };
    std::vector<RGBAf> below(W*H, RGBAf{0,0,0,0});
    // below group: OHR over transparent (pasted hidden; bg transparent)
    compute::HostPlacedLayer vo = view(doc->layers[oi], 0, 0, W, H);
    compute::composite_many_host(below.data(), W, H, 0, 0, W, H, &vo, 1);
    pr("below", below.data(), W, 1280, 700);
    std::vector<RGBAf> grp(W*H, RGBAf{0,0,0,0});
    compute::HostPlacedLayer vi = view(doc->layers[ii], 0, 0, W, H);
    compute::composite_many_host(grp.data(), W, H, 0, 0, W, H, &vi, 1);
    pr("grp", grp.data(), W, 1280, 700);
    compute::applyToneBlend(grp.data(), below.data(), grp.data(), W, H, tp);
    pr("ref-out", grp.data(), W, 1280, 700);
    // App's own composite for comparison.
    doc->rebuildComposite();
    const QColor c = doc->composite.pixelColor(1280, 700);
    std::printf("app: rgba=%d,%d,%d,%d\n", c.red(), c.green(), c.blue(), c.alpha());
    return 0;
}
