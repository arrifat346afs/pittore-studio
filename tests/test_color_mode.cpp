// test_color_mode.cpp — Image > Mode through AppState: the colorMode tag
// lifecycle, Rec.709 grayscale luma, the CMYK separation round trip,
// undo/redo restoring pixels AND the tag/icc fields, live-text colours,
// canvas paper, the 16-bit depth suffix and the no-op guard.
//
// Profile control is deterministic: handing the document its own ICC bytes
// (the system CMYK fixture, when present) pins the conversion to the
// profiled path; without the fixture the resolver falls back to a settings
// or system probe and then the naive full-GCR core — every assertion below
// holds for all three. Runs headless under QCoreApplication; the meson
// XDG_CONFIG_HOME sandbox keeps the real Settings.toml out of the way.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <cstdio>

#include "engine/color/icc_convert.h"
#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/color_mode.h"

using namespace pittore::ui;

namespace {

QImage solid(int w, int h, QRgb c) {
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

bool isRed(const QColor& c) {
    return c.red() > 250 && c.green() < 5 && c.blue() < 5;
}

QByteArray fileBytes(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

// Usable = LCMS2 opens it as a CMYK profile whose encoder self-test passes.
bool usable(const QByteArray& bytes) {
    if (bytes.isEmpty()) return false;
    const pittore::color::SrgbToCmyk probe(
        reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()));
    return probe.valid();
}

}  // namespace

static void test_color_mode() {
    AppState state;
    DocumentItem* d =
        state.addDocument(QStringLiteral("mode"), QSize(32, 24), 300);
    CHECK(d != nullptr);
    if (!d) return;

    d->canvasPaper = QColor(255, 0, 0);
    state.placeImageLayer(solid(8, 8, 0xFFFF0000), QStringLiteral("red"),
                          QPointF(8, 8), 1.0);   // covers x4..11, y4..11
    state.placeImageLayer(solid(8, 8, 0xFFFFFFFF), QStringLiteral("white"),
                          QPointF(24, 8), 1.0);  // covers x20..27, y4..11
    CHECK_EQ(d->layers.size(), 3);
    CHECK(d->colorMode == QStringLiteral("RGB/8"));

    // --- Grayscale: Rec.709 luma into all three channels ------------------
    CHECK(state.convertDocumentMode(AppState::ModeTarget::Grayscale));
    CHECK(d->colorMode == QStringLiteral("Grayscale/8"));
    {
        const QColor g = d->composite.pixelColor(6, 6);
        CHECK(g.red() >= 52 && g.red() <= 56);  // 0.2126 * 255 ≈ 54
        CHECK(g.green() == g.red());
        CHECK(g.blue() == g.red());
        const QColor w = d->composite.pixelColor(22, 6);  // white stays white
        CHECK(w.red() >= 250 && w.green() >= 250 && w.blue() >= 250);
        const QColor p = d->canvasPaper;  // paper rides along
        CHECK(p.red() >= 52 && p.red() <= 56);
        CHECK(p.green() == p.red());
    }
    // Undo restores tag, pixels AND paper; redo re-applies the tag.
    state.undo();
    CHECK(d->colorMode == QStringLiteral("RGB/8"));
    CHECK(isRed(d->composite.pixelColor(6, 6)));
    CHECK_EQ(d->canvasPaper.red(), 255);
    state.redo();
    CHECK(d->colorMode == QStringLiteral("Grayscale/8"));
    state.undo();
    CHECK(d->colorMode == QStringLiteral("RGB/8"));

    // --- CMYK: separation round trip --------------------------------------
    const char* kGs = "/usr/share/ghostscript/iccprofiles/default_cmyk.icc";
    const bool fixture = QFileInfo::exists(kGs);
    if (fixture) {
        d->iccProfile = fileBytes(kGs);
        CHECK(usable(d->iccProfile));
    }
    CHECK(state.convertDocumentMode(AppState::ModeTarget::Cmyk));
    CHECK(d->colorMode == QStringLiteral("CMYK/8"));
    if (!d->iccProfile.isEmpty()) CHECK(usable(d->iccProfile));
    {
        const QColor w = d->composite.pixelColor(22, 6);
        CHECK(w.red() > 215 && w.green() > 215 && w.blue() > 215);
        const QColor r = d->composite.pixelColor(6, 6);
        CHECK(r.red() > r.green() + 90 && r.red() > r.blue() + 90);
        CHECK(d->canvasPaper.red() > d->canvasPaper.green() + 60);
    }

    // --- CMYK -> RGB: retag only (appearance pixels already ARE the RGB) --
    const QColor rAfterCmyk = d->composite.pixelColor(6, 6);
    CHECK(state.convertDocumentMode(AppState::ModeTarget::Rgb));
    CHECK(d->colorMode == QStringLiteral("RGB/8"));
    CHECK(d->iccProfile.isEmpty());  // a stale separation never rides along
    CHECK(d->composite.pixelColor(6, 6) == rAfterCmyk);

    // Undo the retag: back to CMYK with the profile bytes; undo the
    // separation itself: back to RGB with the pre-conversion bytes.
    state.undo();
    CHECK(d->colorMode == QStringLiteral("CMYK/8"));
    if (fixture) CHECK(usable(d->iccProfile));
    state.undo();
    CHECK(d->colorMode == QStringLiteral("RGB/8"));
    if (fixture)
        CHECK(!d->iccProfile.isEmpty());  // snapshot carried them
    else
        CHECK(d->iccProfile.isEmpty());
    CHECK(isRed(d->composite.pixelColor(6, 6)));
    CHECK(isRed(d->canvasPaper));

    // --- The depth suffix survives a conversion ---------------------------
    d->colorMode = QStringLiteral("RGB/16");
    CHECK(state.convertDocumentMode(AppState::ModeTarget::Grayscale));
    CHECK(d->colorMode == QStringLiteral("Grayscale/16"));
    state.undo();
    CHECK(d->colorMode == QStringLiteral("RGB/16"));
    // RGB/32 keeps its suffix on a no-op and clamps to /8 on CMYK (there is
    // no 32-bit CMYK tag, and /32 already exported at 8 anyway).
    d->colorMode = QStringLiteral("RGB/32");
    CHECK(!state.convertDocumentMode(AppState::ModeTarget::Rgb));
    CHECK(state.convertDocumentMode(AppState::ModeTarget::Cmyk));
    CHECK(d->colorMode == QStringLiteral("CMYK/8"));
    state.undo();
    CHECK(d->colorMode == QStringLiteral("RGB/32"));
    d->colorMode = QStringLiteral("RGB/8");

    // --- No-op guard: RGB -> RGB adds no history --------------------------
    const int depth = state.undoDepth();
    CHECK(!state.convertDocumentMode(AppState::ModeTarget::Rgb));
    CHECK_EQ(state.undoDepth(), depth);

    // --- Live text colours ride the conversion (value copies) -------------
    const int ti =
        state.addTextLayer(QPointF(4, 16), 0.0, QStringLiteral("Liberation Sans"),
                           12.0, QColor(255, 0, 0));
    CHECK(ti >= 0);
    if (ti >= 0) {
        CHECK(state.convertDocumentMode(AppState::ModeTarget::Grayscale));
        const QColor tc = d->layers[ti].textSpec.color;
        CHECK(tc.red() >= 52 && tc.red() <= 56);
        CHECK(tc.green() == tc.red());
        CHECK(tc.blue() == tc.red());
        state.undo();
        CHECK(isRed(d->layers[ti].textSpec.color));
        CHECK(d->colorMode == QStringLiteral("RGB/8"));
    }

    // --- Resolver sanity: whatever comes back is a live CMYK profile ------
    {
        const QByteArray bytes = resolveCmykProfile(*d);
        if (!bytes.isEmpty()) CHECK(usable(bytes));
        const QString path = defaultCmykProfilePath();
        if (!path.isEmpty()) CHECK(QFileInfo::exists(path));
    }

    // No active document -> no conversion, no crash.
    AppState bare;
    CHECK(!bare.convertDocumentMode(AppState::ModeTarget::Cmyk));
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-color-mode-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_color_mode();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
