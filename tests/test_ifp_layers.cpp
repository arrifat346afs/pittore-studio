// test_ifp_layers.cpp — IFP v2 bridge round trip (Phase 4B): masks, clipping,
// nesting depth and the transparency lock survive saveProject → openProject.
//
// AppState harness like test_place_ui (headless, scratch XDG_CONFIG_HOME).
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>

#include "engine/compute/layer_mask.h"
#include "engine/core/log.h"
#include "engine/io/psd.h"
#include "test_util.h"
#include "ui/app_state.h"

using namespace pittore::ui;

namespace {

std::shared_ptr<pittore::Image> solidImg(int w, int h, float r, float g,
                                          float b) {
    auto img = std::make_shared<pittore::Image>(
        static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    for (int i = 0; i < w * h; ++i) img->data()[i] = {r, g, b, 1.0f};
    return img;
}

}  // namespace

static void test_ifp_v2_bridges() {
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;

    LayerItem masked;
    masked.name = QStringLiteral("masked");
    masked.kind = LayerItem::Kind::Pixel;
    masked.pixels = solidImg(8, 4, 0, 0, 1);
    masked.sourceStamp = 1;
    masked.lockTransparency = true;
    {
        auto m = std::make_shared<pittore::Image>(8, 4);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 8; ++x)
                m->data()[y * 8 + x] =
                    pittore::compute::make_mask_pixel(x < 4 ? 1.0f : 0.0f);
        masked.mask = std::move(m);
        masked.hasMask = true;
        masked.maskEnabled = false;  // persists disabled
        masked.maskOffset = QPointF(0, 0);
        masked.maskScaleX = masked.maskScaleY = 1.0;
        masked.maskDensity = 0.5f;
        masked.maskFeather = 1.5f;
    }

    LayerItem clipped;
    clipped.name = QStringLiteral("clipped");
    clipped.kind = LayerItem::Kind::Pixel;
    clipped.pixels = solidImg(8, 4, 0, 1, 0);
    clipped.sourceStamp = 1;
    clipped.clipped = true;

    LayerItem group;
    group.name = QStringLiteral("G");
    group.kind = LayerItem::Kind::Group;
    group.indent = 0;

    LayerItem child;
    child.name = QStringLiteral("kid");
    child.kind = LayerItem::Kind::Pixel;
    child.pixels = solidImg(2, 2, 1, 0, 0);
    child.sourceStamp = 1;
    child.indent = 1;

    LayerItem curves;
    curves.name = QStringLiteral("curves");
    curves.kind = LayerItem::Kind::Adjustment;
    curves.adjustmentKind = 3;
    curves.adjustmentCurve = {QPointF(0, 0), QPointF(1, 1)};
    curves.adjustmentCurveR = {QPointF(0, 0), QPointF(1, 0.5)};

    doc->layers = {masked, clipped, group, child, curves};

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;
    const QString path = tmp.filePath(QStringLiteral("v2.ifp"));
    QString error;
    CHECK(state.saveProject(path, &error));
    if (!error.isEmpty())
        std::fprintf(stderr, "    save error: %s\n",
                     error.toLocal8Bit().constData());

    CHECK(state.openProject(path, &error));
    if (!error.isEmpty())
        std::fprintf(stderr, "    open error: %s\n",
                     error.toLocal8Bit().constData());
    DocumentItem* re = state.activeDocument();
    CHECK(re != nullptr);
    if (!re) return;
    CHECK_EQ(re->layers.size(), 5);
    if (re->layers.size() != 5) return;

    const LayerItem& m = re->layers[0];
    CHECK(m.name == QStringLiteral("masked"));
    CHECK(m.lockTransparency);
    CHECK(m.hasMask && m.mask);
    CHECK(!m.maskEnabled);
    CHECK_NEAR(m.maskDensity, 0.5f, 1e-6f);
    CHECK_NEAR(m.maskFeather, 1.5f, 1e-6f);
    if (m.mask) {
        CHECK_EQ(m.mask->width(), 8u);
        CHECK_EQ(m.mask->height(), 4u);
        bool covOk = true;
        for (int y = 0; covOk && y < 4; ++y)
            for (int x = 0; covOk && x < 8; ++x) {
                const float c = m.mask->data()[y * 8 + x].r;
                covOk = (x < 4) ? (c > 0.99f) : (c < 0.01f);
            }
        CHECK(covOk);
    }
    CHECK(m.pixels);
    if (m.pixels) CHECK(m.pixels->data()[0].b > 0.99f);

    CHECK(re->layers[1].clipped);
    CHECK(re->layers[2].kind == LayerItem::Kind::Group);
    CHECK_EQ(re->layers[3].indent, 1);
    CHECK(re->layers[3].pixels);
    CHECK(re->layers[4].kind == LayerItem::Kind::Adjustment);
    CHECK_EQ(re->layers[4].adjustmentKind, 3);
    CHECK_EQ(re->layers[4].adjustmentCurveR.size(), 2);
    CHECK(re->layers[4].adjustmentCurveG.isEmpty());
    CHECK_NEAR(re->layers[4].adjustmentCurveR[1].y(), 0.5, 1e-9);
    CHECK_EQ(re->layers[4].adjustmentLUT.size(), 768u);
}

// Foreign PSD blocks (v4) survive IFP save → open → PSD export while fresh,
// and are dropped at save once the layer is painted.
static void test_ifp_psd_blocks() {
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("b"), QSize(4, 2), 72);
    CHECK(doc != nullptr);
    if (!doc) return;

    LayerItem l;
    l.name = QStringLiteral("type-ish");
    l.kind = LayerItem::Kind::Pixel;
    l.pixels = solidImg(4, 2, 1, 0, 0);
    l.sourceStamp = 1;
    LayerItem::PsdRawBlock rb;
    std::memcpy(rb.sig, "8BIM", 4);
    std::memcpy(rb.key, "TySh", 4);
    rb.data = {9, 8, 7, 6, 5};
    l.psdRawBlocks.push_back(rb);
    l.psdRawSrc = l.sourceStamp;
    l.psdRawAdj = l.adjustStamp;
    l.psdRawMask = l.maskStamp;
    l.psdRawHadMask = l.hasMask;
    l.psdRawKind = l.kind;
    l.psdRawOffset = l.offset;
    l.psdRawScaleX = l.scaleX;
    l.psdRawScaleY = l.scaleY;
    doc->layers = {l};

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;
    QString error;
    const QString path = tmp.filePath(QStringLiteral("b.ifp"));
    CHECK(state.saveProject(path, &error));
    CHECK(state.openProject(path, &error));
    DocumentItem* re = state.activeDocument();
    CHECK(re != nullptr);
    if (!re) return;
    CHECK_EQ(re->layers.size(), 1);
    if (re->layers.size() != 1) return;
    CHECK_EQ(re->layers[0].psdRawBlocks.size(), 1u);
    if (re->layers[0].psdRawBlocks.empty()) return;

    auto bytes = state.exportLayeredPsd(*re, &error);
    CHECK(bytes.has_value());
    if (!bytes) return;
    {
        std::string codecError;
        auto dec = pittore::io::psdDecodeLayers(*bytes, &codecError);
        CHECK(dec.has_value());
        if (!dec) return;
        bool kept = false;
        for (const auto& layer : dec->layers)
            for (const auto& b : layer.rawBlocks)
                if (std::memcmp(b.key, "TySh", 4) == 0 &&
                    b.data == rb.data)
                    kept = true;
        CHECK(kept);
    }

    // Paint after load: the save drops the now-stale block.
    re->layers[0].sourceStamp += 1;
    const QString path2 = tmp.filePath(QStringLiteral("b2.ifp"));
    CHECK(state.saveProject(path2, &error));
    CHECK(state.openProject(path2, &error));
    DocumentItem* re2 = state.activeDocument();
    CHECK(re2 != nullptr);
    if (!re2) return;
    CHECK(re2->layers[0].psdRawBlocks.empty());
    auto bytes2 = state.exportLayeredPsd(*re2, &error);
    CHECK(bytes2.has_value());
    if (!bytes2) return;
    {
        std::string codecError;
        auto dec = pittore::io::psdDecodeLayers(*bytes2, &codecError);
        CHECK(dec.has_value());
        if (!dec) return;
        bool kept = false;
        for (const auto& layer : dec->layers)
            for (const auto& b : layer.rawBlocks)
                if (std::memcmp(b.key, "TySh", 4) == 0) kept = true;
        CHECK(!kept);
    }
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-ifp-layers-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_ifp_v2_bridges();
    test_ifp_psd_blocks();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
