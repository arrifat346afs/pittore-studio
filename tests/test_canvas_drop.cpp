// test_canvas_drop.cpp — dropped-file profile policy: canvas drops run the
// same mismatch resolve as File > Open (ask when pasting).
// Cancel skips the file; family matches stay silent; AlwaysAsk prompts.
// Headless (QT_QPA_PLATFORM=offscreen, scratch XDG_CONFIG_HOME); the sRGB
// fixture is generated in-test via QImageWriter (Qt round-trips the tag).
#include <QApplication>
#include <QColorSpace>
#include <QDir>
#include <QTemporaryDir>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QMimeData>
#include <QUrl>

#include "test_util.h"
#include "ui/app_state.h"
#include "ui/canvas_view.h"
#include "ui/color_mismatch.h"

using namespace pittore::ui;

namespace {

QString writeSrgbPng() {
    const QString path =
        QDir::tempPath() + QStringLiteral("/pittore-drop-srgb.png");
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(QColor(10, 20, 30));
    img.setColorSpace(QColorSpace(QColorSpace::SRgb));
    if (!QImageWriter(path).write(img)) return {};
    // Sanity: the tag must survive the round trip or the cases below are
    // meaningless.
    if (QImage(path).colorSpace().description().isEmpty()) return {};
    return path;
}

int layerCount(AppState& state) {
    DocumentItem* doc = state.activeDocument();
    return doc ? doc->layers.size() : -1;
}

bool dropFile(CanvasView& canvas, const QString& path) {
    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QDropEvent event(canvas.viewport()->rect().center(), Qt::CopyAction, mime,
                     Qt::NoButton, Qt::NoModifier);
    const bool handled = canvas.eventFilter(canvas.viewport(), &event);
    delete mime;
    return handled;
}

}  // namespace

int main(int argc, char** argv) {
    // Never touch the user's real config on manual runs (meson isolates
    // via env; see test_color_policy for why this matters).
    if (qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME")) {
        static QTemporaryDir* scratch = new QTemporaryDir;
        if (scratch->isValid())
            qputenv("XDG_CONFIG_HOME", scratch->path().toLocal8Bit());
    }
    QApplication app(argc, argv);
    const QString png = writeSrgbPng();
    if (png.isEmpty()) {
        std::printf("  (skip: PNG color-space round trip unavailable)\n");
        return 0;
    }

    // Silent Convert fallback (no resolver): placed, no prompt.
    {
        AppState state;
        CHECK(state.addDocument(QStringLiteral("d"), QSize(64, 64), 72) !=
              nullptr);
        CanvasView canvas(&state);
        canvas.resize(800, 600);
        const int before = layerCount(state);
        CHECK(dropFile(canvas, png));
        CHECK(layerCount(state) == before + 1);
    }

    // Cancel skips the file: no layer, no document change.
    {
        AppState state;
        CHECK(state.addDocument(QStringLiteral("d"), QSize(64, 64), 72) !=
              nullptr);
        state.setProfileMismatchResolver(
            [](const QString&, const QString&) {
                return ImportProfileChoice::Cancel;
            });
        CanvasView canvas(&state);
        canvas.resize(800, 600);
        const int before = layerCount(state);
        CHECK(dropFile(canvas, png));
        CHECK(layerCount(state) == before);
    }

    // Ask policy + family match: no prompt, placed.
    {
        AppState state;
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 0;
        state.applySettings(next);
        bool asked = false;
        state.setProfileMismatchResolver(
            [&](const QString&, const QString&) {
                asked = true;
                return ImportProfileChoice::UseEmbedded;
            });
        CHECK(state.addDocument(QStringLiteral("d"), QSize(64, 64), 72) !=
              nullptr);
        CanvasView canvas(&state);
        canvas.resize(800, 600);
        const int before = layerCount(state);
        CHECK(dropFile(canvas, png));
        CHECK(!asked);
        CHECK(layerCount(state) == before + 1);
    }

    // AlwaysAsk: the resolver fires with the embedded tag, choice proceeds.
    {
        AppState state;
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 3;
        state.applySettings(next);
        bool asked = false;
        QString seen;
        state.setProfileMismatchResolver(
            [&](const QString& embedded, const QString&) {
                asked = true;
                seen = embedded;
                return ImportProfileChoice::UseEmbedded;
            });
        CHECK(state.addDocument(QStringLiteral("d"), QSize(64, 64), 72) !=
              nullptr);
        CanvasView canvas(&state);
        canvas.resize(800, 600);
        const int before = layerCount(state);
        CHECK(dropFile(canvas, png));
        CHECK(asked);
        CHECK(!seen.isEmpty());
        CHECK(layerCount(state) == before + 1);
    }

    QFile::remove(png);
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
