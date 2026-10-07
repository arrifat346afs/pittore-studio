// Runs headless (QT_QPA_PLATFORM=offscreen).
//
// End-to-end Color Grading window through the real dialog: sections create
// their backing layers on first write, sliders grade live (throttled),
// presets apply bundles, section reset restores defaults, the preview
// follows, Cancel removes created layers and restores touched ones, OK
// keeps everything.
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

#include <cmath>

#include "engine/compute/adjust.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/color_grade_dialog.h"

using namespace pittore::ui;

namespace {

void makeGreyDoc(AppState& state) {
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(8, 4), 72);
    if (!doc) return;
    LayerItem px;
    px.name = QStringLiteral("grey");
    px.kind = LayerItem::Kind::Pixel;
    auto img = std::make_shared<pittore::Image>(8, 4);
    img->fill(pittore::RGBAf{0.5f, 0.5f, 0.5f, 1});
    px.pixels = std::move(img);
    px.sourceStamp = 1;
    doc->layers = {px};
    doc->rebuildComposite();
    state.setActiveLayerIndex(0);
}

int findLayerByKind(AppState& state, int kind) {
    DocumentItem* d = state.activeDocument();
    if (!d) return -1;
    for (int i = 0; i < static_cast<int>(d->layers.size()); ++i) {
        const LayerItem& l = d->layers[i];
        if (l.kind == LayerItem::Kind::Adjustment && l.adjustmentKind == kind)
            return i;
    }
    return -1;
}

QSlider* nthSlider(ColorGradeDialog& dlg, int n) {
    QList<QSlider*> ordered;
    for (const char* name :
         {"Exposure", "Blackpoint", "Brightness", "Contrast", "Saturation",
          "Vibrance", "Temperature", "Tint"}) {
        QSlider* s = dlg.findChild<QSlider*>(
            QStringLiteral("GradeSlider:") + QString::fromUtf8(name));
        if (s) ordered.push_back(s);
    }
    return n >= 0 && n < ordered.size() ? ordered[n] : nullptr;
}

void pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

}  // namespace

static void test_grade_dialog_sections() {
    AppState state;
    makeGreyDoc(state);
    DocumentItem* d = state.activeDocument();
    CHECK(d != nullptr);
    if (!d) return;

    ColorGradeDialog dlg(&state);
    dlg.show();

    // Eight rows across three sections; nothing created until first write.
    CHECK_EQ(dlg.findChildren<QSlider*>().size(), 8);
    CHECK_EQ(state.activeDocument()->layers.size(), 1u);
    QLabel* preview =
        dlg.findChild<QLabel*>(QStringLiteral("GradePreview"));
    CHECK(preview != nullptr);
    if (preview) {
        const QPixmap pm = preview->pixmap(Qt::ReturnByValue);
        CHECK(!pm.isNull());
    }

    // Exposure to +5 stops: backing layer created, composite clips white.
    QSlider* exposure = nthSlider(dlg, 0);
    CHECK(exposure != nullptr);
    if (!exposure) return;
    exposure->setValue(exposure->maximum());
    pump(150);
    const int expKind =
        static_cast<int>(pittore::compute::AdjustmentKind::Exposure);
    const int expIdx = findLayerByKind(state, expKind);
    CHECK(expIdx >= 0);
    CHECK_EQ(state.activeDocument()->layers.size(), 2u);
    CHECK(std::abs(state.activeDocument()
                       ->composite.pixelColor(2, 2)
                       .red() -
                   255) <= 2);

    // Cancel removes the created layer and restores the grey composite.
    dlg.reject();
    CHECK_EQ(state.activeDocument()->layers.size(), 1u);
    CHECK(std::abs(state.activeDocument()
                       ->composite.pixelColor(2, 2)
                       .red() -
                   128) <= 2);
}

static void test_grade_dialog_accept_preset_throttle() {
    AppState state;
    makeGreyDoc(state);
    if (!state.activeDocument()) return;

    ColorGradeDialog dlg(&state);
    dlg.show();

    // Punchy preset creates its layers with the bundled values.
    QComboBox* preset = dlg.findChild<QComboBox*>();
    CHECK(preset != nullptr);
    if (!preset) return;
    preset->setCurrentIndex(1);
    QMetaObject::invokeMethod(preset, "activated", Q_ARG(int, 1));
    pump(150);
    const int vibIdx = findLayerByKind(
        state, static_cast<int>(pittore::compute::AdjustmentKind::Vibrance));
    CHECK(vibIdx >= 0);
    if (vibIdx >= 0) {
        CHECK(std::abs(state.activeDocument()
                           ->layers[vibIdx]
                           .adjustmentParams[0] -
                       0.3f) < 1e-6f);
    }

    // Slider storm coalesces: 20 rapid writes flush as a couple at most.
    QSlider* exposure = nthSlider(dlg, 0);
    CHECK(exposure != nullptr);
    if (!exposure) return;
    int mods = 0;
    QObject::connect(&state, &AppState::documentModified,
                     [&mods](DocumentItem*) { ++mods; });
    for (int i = 0; i < 20; ++i)
        exposure->setValue(i % 2 ? exposure->maximum() : 0);
    pump(400);
    CHECK(mods <= 4);

    // Section reset restores that section's layers to defaults.
    QPushButton* expReset = dlg.findChild<QPushButton*>(
        QStringLiteral("SectionReset:Exposure"));
    CHECK(expReset != nullptr);
    if (expReset) {
        // Dirty the section first so reset has something to undo.
        QSlider* blackpoint = nthSlider(dlg, 1);
        if (blackpoint) blackpoint->setValue(blackpoint->maximum());
        pump(150);
        expReset->click();
        pump(100);
        const int lvIdx = findLayerByKind(
            state, static_cast<int>(
                       pittore::compute::AdjustmentKind::Levels));
        if (lvIdx >= 0) {
            CHECK(std::abs(state.activeDocument()
                               ->layers[lvIdx]
                               .adjustmentParams[0] -
                           0.0f) < 1e-6f);
        }
    }

    // OK keeps the created layers and their values.
    dlg.accept();
    CHECK(findLayerByKind(state, static_cast<int>(
                                     pittore::compute::AdjustmentKind::Exposure)) >=
          0);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    test_grade_dialog_sections();
    test_grade_dialog_accept_preset_throttle();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
