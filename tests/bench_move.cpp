// bench_move.cpp — per-frame cost of a placed-layer move and resize through the
// real AppState / DocumentItem / compute-backend path, plus a raw backend
// breakdown (clear / placed kernels / ARGB blit). Doubles as the regression
// check for the interactive move/resize budget: run it and read ms/frame.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "engine/compute/factory.h"
#include "engine/core/log.h"
#include "engine/core/pixel.h"
#include "ui/app_state.h"

using namespace pittore::ui;

// Direct GPU breakdown: time (clear + N placed kernels + ARGB blit) for a
// full-frame region, the exact per-move sequence renderRegion issues.
static void micro() {
    using namespace pittore::compute;
    auto gpu = make_backend(BackendType::CUDA);
    if (!gpu) gpu = make_backend(BackendType::HIP);
    if (!gpu) {
        std::printf("micro: no GPU\n");
        return;
    }
    const std::uint32_t w = 1920, h = 1080;
    auto acc = gpu->make_buffer(std::size_t(w) * h * sizeof(pittore::RGBAf));
    auto bg = gpu->make_buffer(std::size_t(w) * h * sizeof(pittore::RGBAf));
    auto photo = gpu->make_buffer(std::size_t(3840) * 2160 * sizeof(pittore::RGBAf));
    auto* bgpx = static_cast<pittore::RGBAf*>(bg->host());
    for (std::size_t i = 0; i < std::size_t(w) * h; ++i)
        bgpx[i] = pittore::RGBAf{0.5f, 0.5f, 0.5f, 1.0f};
    bg->upload();
    photo->upload();
    std::vector<std::uint8_t> out(std::size_t(w) * h * 4);

    auto run = [&](const char* label, bool doBg, bool doPhoto, int n,
                   std::uint32_t rx0 = 0, std::uint32_t ry0 = 0,
                   std::uint32_t rx1 = 1920, std::uint32_t ry1 = 1080) {
        QElapsedTimer t;
        t.start();
        for (int f = 0; f < n; ++f) {
            gpu->clear(*acc);
            if (doBg)
                gpu->composite_placed_into(*acc, *bg, w, h, 0, 0, 1.0, 1.0, w, rx0,
                                           ry0, rx1, ry1, 1.0f, BlendMode::Normal);
            if (doPhoto)
                gpu->composite_placed_into(*acc, *photo, 3840, 2160, 0, 0, 0.5, 0.5,
                                           w, rx0, ry0, rx1, ry1, 1.0f,
                                           BlendMode::Normal);
            std::uint8_t* dst = out.data() + std::size_t(ry0) * w * 4 + rx0 * 4;
            gpu->blit_premul(*acc, dst, w, rx0, ry0, rx1, ry1, std::size_t(w) * 4);
        }
        std::printf("  micro %-22s %.3f ms/frame\n", label, t.nsecsElapsed() / 1e6 / n);
    };
    run("full clear+blit", false, false, 200);
    run("full clear+bg+photo+blit", true, true, 200);
    // Narrow slice, like a per-move dirty rect: same 2 layers, 500x280 region.
    run("slice 500x280 2-layer", true, true, 200, 1200, 650, 1700, 930);
    run("slice 500x280 blit only", false, false, 200, 1200, 650, 1700, 930);
}

static double timeMoves(AppState& state, const QPointF& base, double scale, int n) {
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < n; ++i) {
        const double dx = (i % 2) ? 1.0 : -1.0;
        const double dy = (i % 4 < 2) ? 1.0 : -1.0;
        state.setActiveLayerPlacement(QPointF(base.x() + dx, base.y() + dy), scale,
                                      scale);
    }
    return t.nsecsElapsed() / 1e6 / n;
}

static double timeScales(AppState& state, const QPointF& base, double scale, int n) {
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < n; ++i) {
        const double s = scale * ((i % 2) ? 1.002 : 0.998);
        state.setActiveLayerPlacement(base, s, s);
    }
    return t.nsecsElapsed() / 1e6 / n;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-bench-move-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());

    const QString path =
        argc > 1 ? QString::fromLocal8Bit(argv[1])
                 : QString::fromLocal8Bit(qgetenv("PITTORE_BENCH_IMAGE"));
    if (path.isEmpty()) {
        std::fprintf(stderr, "usage: bench_move <image>  (or set PITTORE_BENCH_IMAGE)\n");
        return 2;
    }
    QImage img(path);
    if (img.isNull()) {
        std::fprintf(stderr, "cannot load %s\n", qPrintable(path));
        return 2;
    }
    std::printf("image %dx%d (%.1f MB decoded)\n", img.width(), img.height(),
                img.width() * double(img.height()) * 4.0 / 1048576.0);

    AppState state;
    AppSettings settings = state.settings();
    settings.gpuEnabled = true;
    state.applySettings(settings);
    std::printf("backend = %s\n", state.computeDeviceLabel().toLocal8Bit().constData());
    micro();

    DocumentItem* d = state.addDocument(QStringLiteral("bench"), QSize(1920, 1080), 300);
    if (!d) return 3;

    const double fit = std::min(1920.0 / img.width(), 1080.0 / img.height());
    const QPointF center(960, 540);
    state.placeImageLayer(img, QStringLiteral("photo"), center, fit);
    LayerItem* l = state.activeLayer();
    if (!l) return 4;
    std::printf("placed at %.3f scale (doc %.0fx%.0f), %d layers\n", fit,
                img.width() * fit, img.height() * fit, static_cast<int>(d->layers.size()));

    // Warm-up (fills caches / culls transparent work).
    timeMoves(state, l->offset, fit, 10);

    const double move1 = timeMoves(state, l->offset, fit, 300);
    const double scale1 = timeScales(state, l->offset, fit, 200);
    // Second larger move sweep, in case a cache was cold.
    const double move2 = timeMoves(state, l->offset, fit, 300);

    std::printf("\n  move  (1px sweep): %.3f ms/frame  (%.0f fps)\n", move1,
                1000.0 / move1);
    std::printf("  move  (repeat)   : %.3f ms/frame  (%.0f fps)\n", move2,
                1000.0 / move2);
    std::printf("  scale (0.2%% step) : %.3f ms/frame  (%.0f fps)\n", scale1,
                1000.0 / scale1);
    return 0;
}
