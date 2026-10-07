#include "ui/layer_style_dialog.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "ui/app_state.h"

namespace pittore::ui {
namespace {

using render::LayerStyle;
using render::StyleBlend;
using render::StyleColor;

const QVector<QPair<QString, StyleBlend>>& blendNames() {
    static const QVector<QPair<QString, StyleBlend>> names = {
        {QObject::tr("Normal"), StyleBlend::Normal},
        {QObject::tr("Multiply"), StyleBlend::Multiply},
        {QObject::tr("Screen"), StyleBlend::Screen},
        {QObject::tr("Overlay"), StyleBlend::Overlay},
        {QObject::tr("Darken"), StyleBlend::Darken},
        {QObject::tr("Lighten"), StyleBlend::Lighten},
        {QObject::tr("Color Burn"), StyleBlend::ColorBurn},
        {QObject::tr("Color Dodge"), StyleBlend::ColorDodge},
        {QObject::tr("Hard Light"), StyleBlend::HardLight},
        {QObject::tr("Soft Light"), StyleBlend::SoftLight},
        {QObject::tr("Difference"), StyleBlend::Difference},
        {QObject::tr("Exclusion"), StyleBlend::Exclusion},
        {QObject::tr("Add"), StyleBlend::Add},
        {QObject::tr("Subtract"), StyleBlend::Subtract},
    };
    return names;
}

QColor toQColor(const StyleColor& c) {
    return QColor::fromRgbF(std::clamp(c.r, 0.0f, 1.0f), std::clamp(c.g, 0.0f, 1.0f),
                            std::clamp(c.b, 0.0f, 1.0f), std::clamp(c.a, 0.0f, 1.0f));
}

StyleColor fromQColor(const QColor& c) {
    return StyleColor{static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                      static_cast<float>(c.blueF()), static_cast<float>(c.alphaF())};
}

// A colour swatch that opens a colour picker (alpha included) on click.
class ColorButton final : public QToolButton {
  public:
    ColorButton(QColor c, std::function<void(QColor)> onPick, QWidget* parent)
        : QToolButton(parent), color_(std::move(c)), onPick_(std::move(onPick)) {
        setFixedSize(72, 24);
        setCursor(Qt::PointingHandCursor);
        connect(this, &QToolButton::clicked, this, [this] {
            const QColor picked = QColorDialog::getColor(
                color_, this, QObject::tr("Select Color"),
                QColorDialog::ShowAlphaChannel);
            if (!picked.isValid()) return;
            color_ = picked;
            update();
            onPick_(picked);
        });
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect().adjusted(1, 1, -1, -1), color_);
        p.setPen(QPen(palette().color(QPalette::Mid), 1));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
    }

  private:
    QColor color_;
    std::function<void(QColor)> onPick_;
};

// Slider + spin box acting as one control (the conventional numeric field).
class SliderSpin final : public QWidget {
  public:
    SliderSpin(double min, double max, double value, int decimals, double step,
               const QString& suffix, std::function<void(double)> onChanged,
               QWidget* parent = nullptr)
        : QWidget(parent), onChanged_(std::move(onChanged)) {
        const double factor = std::pow(10.0, decimals);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);
        slider_ = new QSlider(Qt::Horizontal, this);
        styleSlider(slider_);
        slider_->setRange(static_cast<int>(std::lround(min * factor)),
                          static_cast<int>(std::lround(max * factor)));
        slider_->setValue(static_cast<int>(std::lround(value * factor)));
        spin_ = new QDoubleSpinBox(this);
        spin_->setRange(min, max);
        spin_->setDecimals(decimals);
        spin_->setSingleStep(step);
        spin_->setSuffix(suffix);
        spin_->setValue(value);
        spin_->setFixedWidth(96);
        spin_->setAlignment(Qt::AlignRight);
        row->addWidget(slider_, 1);
        row->addWidget(spin_);
        connect(slider_, &QSlider::valueChanged, this, [this, factor](int v) {
            QSignalBlocker block(spin_);
            spin_->setValue(v / factor);
            onChanged_(v / factor);
        });
        connect(spin_, &QDoubleSpinBox::valueChanged, this, [this, factor](double v) {
            QSignalBlocker block(slider_);
            slider_->setValue(static_cast<int>(std::lround(v * factor)));
            onChanged_(v);
        });
    }

  private:
    QSlider* slider_ = nullptr;
    QDoubleSpinBox* spin_ = nullptr;
    std::function<void(double)> onChanged_;

    static void styleSlider(QSlider* slider) {
        const QPalette pal = slider->palette();
        const QString accent = pal.color(QPalette::Highlight).name();
        const QString groove = pal.color(QPalette::Midlight).name();
        const QString edge = pal.color(QPalette::Mid).name();
        slider->setStyleSheet(
            QStringLiteral("QSlider::groove:horizontal { background: %1; height: 4px; border-radius: 2px; }"
                           "QSlider::sub-page:horizontal { background: %2; height: 4px; border-radius: 2px; }"
                           "QSlider::handle:horizontal { background: #e8e8e8; width: 12px; margin: -5px 0; "
                           "border-radius: 6px; border: 1px solid %3; }"
                           "QSlider::handle:horizontal:hover { background: #ffffff; }")
                .arg(groove, accent, edge));
    }
};

SliderSpin* percentRow(double value, std::function<void(double)> f, QWidget* parent) {
    return new SliderSpin(0.0, 100.0, value, 0, 1.0, QStringLiteral("%"),
                          std::move(f), parent);
}

SliderSpin* pixelRow(double value, std::function<void(double)> f, QWidget* parent) {
    return new SliderSpin(0.0, 250.0, value, 1, 1.0, QStringLiteral(" px"),
                          std::move(f), parent);
}

SliderSpin* degreeRow(double value, std::function<void(double)> f, QWidget* parent) {
    return new SliderSpin(0.0, 360.0, value, 0, 1.0, QStringLiteral("°"),
                          std::move(f), parent);
}

QComboBox* blendRow(StyleBlend value, std::function<void(StyleBlend)> f, QWidget* parent) {
    auto* box = new QComboBox(parent);
    for (const auto& entry : blendNames()) box->addItem(entry.first, static_cast<int>(entry.second));
    const int at = box->findData(static_cast<int>(value));
    box->setCurrentIndex(at < 0 ? 0 : at);
    QObject::connect(box, qOverload<int>(&QComboBox::currentIndexChanged), box,
                     [box, f = std::move(f)](int) {
                         f(static_cast<StyleBlend>(box->currentData().toInt()));
                     });
    return box;
}

ColorButton* colorRow(const StyleColor& value, std::function<void(StyleColor)> f,
                      QWidget* parent) {
    return new ColorButton(toQColor(value),
                           [f = std::move(f)](QColor c) { f(fromQColor(c)); }, parent);
}

// A scrolling settings page with `form` already laid out inside it.
QScrollArea* formPage(QFormLayout** form, QWidget* parent) {
    auto* content = new QWidget;
    auto* layout = new QFormLayout(content);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    layout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    *form = layout;
    return scroll;
}

int effectCount() { return static_cast<int>(StyleEffect::Count); }

// Proxy bake long edge (px) for live drag previews: only layers bigger
// than this ever leave full res, and even then the proxy stays sharp
// enough to judge the effect. The dialog settles to full resolution on
// idle/accept/reject.
constexpr double kFxProxyCap = 1024.0;
// Past this many bake pixels a full bake no longer fits the frame budget,
// so bursts drop to the proxy cap until idle. Kept at cap² so the proxy
// only engages when the bake would actually shrink.
constexpr std::uint64_t kFxProxyPixels = 1024 * 1024;

}  // namespace

LayerStyleDialog::LayerStyleDialog(AppState* state, QWidget* parent)
    : QDialog(parent), state_(state) {
    doc_ = state_->activeDocument();
    targetIndices_ = state_->fxTargetLayers();
    if (doc_ && !targetIndices_.isEmpty()) {
        originals_.reserve(targetIndices_.size());
        for (int idx : targetIndices_)
            originals_.push_back(doc_->layers[idx].style);
        work_ = originals_.front();
    }
    setWindowTitle(targetIndices_.size() > 1
                       ? tr("Layer Effects — %1 layers").arg(targetIndices_.size())
                       : tr("Layer Effects"));
    resize(880, 620);
    // One history step for the whole dialog session: the pre-dialog style is
    // snapshotted now, the live preview edits happen against it, and OK
    // promotes the step while Cancel restores and drops it.
    state_->beginUndoStep();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    buildEffectList(this);
    buildPages(this);
    body->addWidget(list_, 0);
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::NoFrame);
    sep->setFixedWidth(1);
    sep->setStyleSheet(QStringLiteral("QFrame { background: %1; border: none; }").arg(cssColor(QColor(255, 255, 255, kDividerAlpha))));
    body->addWidget(sep);
    body->addWidget(stack_, 1);
    root->addLayout(body, 1);

    auto* footer = new QHBoxLayout;
    footerStatus_ = new QLabel(this);
    footerStatus_->setStyleSheet(
        QStringLiteral("color: %1;").arg(palette().color(QPalette::PlaceholderText).name()));
    footer->addWidget(footerStatus_);
    footer->addStretch(1);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok)) ok->setDefault(true);
    footer->addWidget(buttons);
    root->addLayout(footer);
    connect(buttons, &QDialogButtonBox::accepted, this, &LayerStyleDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &LayerStyleDialog::reject);

    // Seed the check states from the layer's current style without previewing.
    loading_ = true;
    const bool on[10] = {
        work_.hasBevel,    work_.hasStroke,        work_.hasInnerShadow,
        work_.hasInnerGlow, work_.hasSatin,        work_.hasColorOverlay,
        work_.hasGradient, work_.hasOuterGlow,     work_.hasDropShadow,
        work_.hasBlur,
    };
    for (int i = 0; i < effectCount(); ++i)
        list_->item(i)->setCheckState(on[i] ? Qt::Checked : Qt::Unchecked);
    loading_ = false;
    updateFooterStatus();

    int first = 0;
    for (int i = 0; i < effectCount(); ++i)
        if (on[i]) {
            first = i;
            break;
        }
    list_->setCurrentRow(first);
    stack_->setCurrentIndex(first);
}

LayerStyleDialog::~LayerStyleDialog() = default;

void LayerStyleDialog::buildEffectList(QWidget* parent) {
    list_ = new QListWidget(parent);
    list_->setObjectName(QStringLiteral("fxList"));
    list_->setFixedWidth(220);
    list_->setUniformItemSizes(true);
    list_->setSpacing(4);
    const QColor accent = palette().color(QPalette::Highlight);
    const QColor border = palette().color(QPalette::Mid);
    list_->setStyleSheet(
        QStringLiteral("QListWidget#fxList { border: 1px solid %1; border-radius: 4px; padding: 4px; }"
                       "QListWidget#fxList::item { padding: 6px 8px; border: none; border-radius: 3px; }"
                       "QListWidget#fxList::item:selected { background: %2; color: #ffffff; }")
            .arg(border.name(), accent.name()));
    const QStringList names = {
        tr("Bevel & Emboss"),   tr("Stroke"),        tr("Inner Shadow"),
        tr("Inner Glow"),       tr("Satin"),         tr("Color Overlay"),
        tr("Gradient Overlay"), tr("Outer Glow"),    tr("Drop Shadow"),
        tr("Blur"),
    };
    for (const QString& name : names) {
        auto* item = new QListWidgetItem(name, list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) stack_->setCurrentIndex(row);
    });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (loading_) return;
        const int row = list_->row(item);
        if (row < 0) return;
        setEffectEnabled(static_cast<StyleEffect>(row),
                         item->checkState() == Qt::Checked);
    });
}

void LayerStyleDialog::buildPages(QWidget* parent) {
    stack_ = new QStackedWidget(parent);
    stack_->addWidget(buildBevelPage());
    stack_->addWidget(buildStrokePage());
    stack_->addWidget(buildShadowPage(true));
    stack_->addWidget(buildGlowPage(true));
    stack_->addWidget(buildSatinPage());
    stack_->addWidget(buildColorOverlayPage());
    stack_->addWidget(buildGradientOverlayPage());
    stack_->addWidget(buildGlowPage(false));
    stack_->addWidget(buildShadowPage(false));
    stack_->addWidget(buildBlurPage());
}

void LayerStyleDialog::setEffectEnabled(StyleEffect effect, bool on) {
    switch (effect) {
        case StyleEffect::Bevel: work_.hasBevel = on; break;
        case StyleEffect::Stroke: work_.hasStroke = on; break;
        case StyleEffect::InnerShadow: work_.hasInnerShadow = on; break;
        case StyleEffect::InnerGlow: work_.hasInnerGlow = on; break;
        case StyleEffect::Satin: work_.hasSatin = on; break;
        case StyleEffect::ColorOverlay: work_.hasColorOverlay = on; break;
        case StyleEffect::GradientOverlay: work_.hasGradient = on; break;
        case StyleEffect::OuterGlow: work_.hasOuterGlow = on; break;
        case StyleEffect::DropShadow: work_.hasDropShadow = on; break;
        case StyleEffect::Blur: work_.hasBlur = on; break;
        case StyleEffect::Count: break;
    }
    changed_ = true;
    updateFooterStatus();
    schedulePreview();
}

void LayerStyleDialog::updateFooterStatus() {
    if (!footerStatus_) return;
    int enabled = 0;
    for (int i = 0; i < effectCount() && i < list_->count(); ++i)
        if (list_->item(i)->checkState() == Qt::Checked) ++enabled;
    footerStatus_->setText(tr("%1 of %2 effects enabled").arg(enabled).arg(effectCount()));
}

void LayerStyleDialog::selectEffect(StyleEffect effect) {
    if (!list_) return;
    const int row = static_cast<int>(effect);
    if (row < 0 || row >= effectCount()) return;
    loading_ = true;
    list_->item(row)->setCheckState(Qt::Checked);
    loading_ = false;
    setEffectEnabled(effect, true);
    list_->setCurrentRow(row);
}

void LayerStyleDialog::schedulePreview() {
    if (loading_) return;
    // Interval throttle with a leading edge: the first tweak of a burst
    // renders immediately, further ticks set the pending flag the running
    // timer picks up about every 66ms, so a drag previews live at ~15fps
    // instead of starving (restarting single-shot) or queueing a full bake
    // per tick. Heavy bursts bake a proxy and settle to full res on idle;
    // light ones stay full-res throughout and never visibly swap.
    previewPending_ = true;
    if (!previewTimer_) {
        previewTimer_ = new QTimer(this);
        previewTimer_->setInterval(66);
        connect(previewTimer_, &QTimer::timeout, this, [this] {
            if (previewPending_) {
                previewPending_ = false;
                previewNow();
            } else if (proxyActive_) {
                // Idle: the storm is over — settle to full resolution.
                previewTimer_->stop();
                proxyActive_ = false;
                settleFullPreview();
            } else {
                previewTimer_->stop();
            }
        });
    }
    if (!previewTimer_->isActive()) {
        proxyActive_ = wantProxyPreview();
        setStyledBakeCap(proxyActive_ ? kFxProxyCap : 2048.0);
        previewPending_ = false;
        previewNow();
        previewTimer_->start();
    }
}

// A proxy only pays off past a few hundred thousand bake pixels (below that
// a full bake fits comfortably inside the frame budget, so swapping
// resolutions would cost a visible pop for no responsiveness gain).
bool LayerStyleDialog::wantProxyPreview() const {
    if (!doc_) return false;
    std::uint64_t pixels = 0;
    for (int i = 0; i < targetIndices_.size(); ++i) {
        const LayerItem* layer = targetAt(i);
        if (!layer || !layer->pixels) continue;
        const double dw =
            std::ceil(layer->pixels->width() * std::max(0.01, layer->scaleX));
        const double dh =
            std::ceil(layer->pixels->height() * std::max(0.01, layer->scaleY));
        pixels += static_cast<std::uint64_t>(dw * dh);
        if (pixels > kFxProxyPixels) return true;
    }
    return false;
}

// Full-resolution settle after a proxy storm: the proxy bake poisoned the
// styled cache (its key ignores resolution), so invalidate and re-bake at
// the restored cap. No style values change here.
void LayerStyleDialog::settleFullPreview() {
    setStyledBakeCap(2048.0);
    if (!doc_ || targetIndices_.isEmpty()) return;
    for (int i = 0; i < targetIndices_.size(); ++i) {
        LayerItem* layer = targetAt(i);
        if (!layer) continue;
        layer->styledValid = false;
    }
    doc_->rebuildComposite();
    emit state_->documentModified(doc_);
}

void LayerStyleDialog::previewNow() {
    if (!doc_ || targetIndices_.isEmpty()) return;
    for (int i = 0; i < targetIndices_.size(); ++i) {
        LayerItem* layer = targetAt(i);
        if (!layer) continue;
        layer->style = work_;
        ++layer->sourceStamp;   // the styled cache and its device copy are stale
        layer->styledValid = false;
    }
    doc_->rebuildComposite();
    emit state_->documentModified(doc_);
}

int LayerStyleDialog::targetCount() const { return targetIndices_.size(); }

LayerItem* LayerStyleDialog::targetAt(int i) const {
    if (!doc_ || i < 0 || i >= targetIndices_.size()) return nullptr;
    const int idx = targetIndices_.at(i);
    if (idx < 0 || idx >= doc_->layers.size()) return nullptr;
    return &doc_->layers[idx];
}

void LayerStyleDialog::accept() {
    if (previewTimer_) previewTimer_->stop();
    previewPending_ = false;
    setStyledBakeCap(2048.0);
    if (!doc_ || targetIndices_.isEmpty()) {
        state_->discardUndoStep();
        QDialog::accept();
        return;
    }
    previewNow();
    if (changed_)
        state_->commitUndoStep(tr("Layer Style"), QStringLiteral("fx"));
    else
        state_->discardUndoStep();
    QDialog::accept();
}

void LayerStyleDialog::reject() {
    if (previewTimer_) previewTimer_->stop();
    previewPending_ = false;
    setStyledBakeCap(2048.0);
    if (doc_ && !targetIndices_.isEmpty()) {
        for (int i = 0; i < targetIndices_.size() && i < originals_.size(); ++i) {
            LayerItem* layer = targetAt(i);
            if (!layer) continue;
            layer->style = originals_.at(i);
            ++layer->sourceStamp;
            layer->styledValid = false;
        }
        doc_->rebuildComposite();
        emit state_->documentModified(doc_);
    }
    state_->discardUndoStep();
    QDialog::reject();
}

QWidget* LayerStyleDialog::buildBevelPage() {
    QFormLayout* form = nullptr;
    QScrollArea* page = formPage(&form, this);
    render::BevelStyle& b = work_.bevel;

    auto* style = new QComboBox(page);
    style->addItems({tr("Outer Bevel"), tr("Inner Bevel"), tr("Emboss"),
                     tr("Pillow Emboss")});
    style->setCurrentIndex(std::clamp(b.style, 0, 3));
    connect(style, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        work_.bevel.style = i;
        changed_ = true;
        schedulePreview();
    });
    form->addRow(tr("Style:"), style);

    form->addRow(tr("Depth:"),
                 percentRow(std::abs(b.depth) * 100.0,
                            [this](double v) {
                                const double sign =
                                    work_.bevel.depth < 0.0 ? -1.0 : 1.0;
                                work_.bevel.depth = sign * v / 100.0;
                                changed_ = true;
                                schedulePreview();
                            },
                            page));
    auto* invert = new QCheckBox(tr("Invert"), page);
    invert->setChecked(b.depth < 0.0);
    connect(invert, &QCheckBox::toggled, this, [this](bool on) {
        work_.bevel.depth = std::abs(work_.bevel.depth) * (on ? -1.0 : 1.0);
        changed_ = true;
        schedulePreview();
    });
    form->addRow(QString(), invert);

    form->addRow(tr("Size:"),
                 pixelRow(b.size, [this](double v) {
                     work_.bevel.size = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Soften:"),
                 pixelRow(b.soften, [this](double v) {
                     work_.bevel.soften = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Angle:"),
                 degreeRow(b.angle, [this](double v) {
                     work_.bevel.angle = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Altitude:"),
                 new SliderSpin(0.0, 90.0, b.altitude, 0, 1.0, QStringLiteral("°"),
                                [this](double v) {
                                    work_.bevel.altitude = static_cast<float>(v);
                                    changed_ = true;
                                    schedulePreview();
                                },
                                page));

    form->addRow(tr("Highlight:"),
                 colorRow(b.highlight, [this](StyleColor c) {
                     work_.bevel.highlight = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Highlight mode:"),
                 blendRow(b.highlightBlend, [this](StyleBlend m) {
                     work_.bevel.highlightBlend = m;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Highlight opacity:"),
                 percentRow(b.highlightOpacity * 100.0, [this](double v) {
                     work_.bevel.highlightOpacity = static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Shadow:"),
                 colorRow(b.shadow, [this](StyleColor c) {
                     work_.bevel.shadow = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Shadow mode:"),
                 blendRow(b.shadowBlend, [this](StyleBlend m) {
                     work_.bevel.shadowBlend = m;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Shadow opacity:"),
                 percentRow(b.shadowOpacity * 100.0, [this](double v) {
                     work_.bevel.shadowOpacity = static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    return page;
}

QWidget* LayerStyleDialog::buildStrokePage() {
    QFormLayout* form = nullptr;
    QScrollArea* page = formPage(&form, this);
    render::StrokeStyle& s = work_.stroke;

    form->addRow(tr("Size:"),
                 pixelRow(s.size, [this](double v) {
                     work_.stroke.size = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    auto* position = new QComboBox(page);
    position->addItems({tr("Outside"), tr("Center"), tr("Inside")});
    position->setCurrentIndex(std::clamp(s.position, 0, 2));
    connect(position, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) {
                work_.stroke.position = i;
                changed_ = true;
                schedulePreview();
            });
    form->addRow(tr("Position:"), position);
    form->addRow(tr("Blend Mode:"),
                 blendRow(s.blend, [this](StyleBlend m) {
                     work_.stroke.blend = m;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Opacity:"),
                 percentRow(s.opacity * 100.0, [this](double v) {
                     work_.stroke.opacity = static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Color:"),
                 colorRow(s.color, [this](StyleColor c) {
                     work_.stroke.color = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    return page;
}

QWidget* LayerStyleDialog::buildShadowPage(bool inner) {
    QFormLayout* form = nullptr;
    QScrollArea* page = formPage(&form, this);
    render::ShadowStyle& s = inner ? work_.innerShadow : work_.dropShadow;

    form->addRow(tr("Blend Mode:"),
                 blendRow(s.blend, [this, inner](StyleBlend m) {
                     (inner ? work_.innerShadow : work_.dropShadow).blend = m;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Color:"),
                 colorRow(s.color, [this, inner](StyleColor c) {
                     (inner ? work_.innerShadow : work_.dropShadow).color = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Opacity:"),
                 percentRow(s.opacity * 100.0, [this, inner](double v) {
                     (inner ? work_.innerShadow : work_.dropShadow).opacity =
                         static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Angle:"),
                 degreeRow(s.angle, [this, inner](double v) {
                     (inner ? work_.innerShadow : work_.dropShadow).angle =
                         static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Distance:"),
                 pixelRow(s.distance, [this, inner](double v) {
                     (inner ? work_.innerShadow : work_.dropShadow).distance =
                         static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Spread:"),
                 percentRow(s.spread * 100.0, [this, inner](double v) {
                     (inner ? work_.innerShadow : work_.dropShadow).spread =
                         static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Size:"),
                 pixelRow(s.size, [this, inner](double v) {
                     (inner ? work_.innerShadow : work_.dropShadow).size =
                         static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    if (!inner) {
        auto* knockout = new QCheckBox(tr("Layer Knocks Out Drop Shadow"), page);
        knockout->setChecked(s.knockout);
        connect(knockout, &QCheckBox::toggled, this, [this](bool on) {
            work_.dropShadow.knockout = on;
            changed_ = true;
            schedulePreview();
        });
        form->addRow(QString(), knockout);
    }
    return page;
}

QWidget* LayerStyleDialog::buildGlowPage(bool inner) {
    QFormLayout* form = nullptr;
    QScrollArea* page = formPage(&form, this);
    render::GlowStyle& g = inner ? work_.innerGlow : work_.outerGlow;

    form->addRow(tr("Blend Mode:"),
                 blendRow(g.blend, [this, inner](StyleBlend m) {
                     (inner ? work_.innerGlow : work_.outerGlow).blend = m;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Color:"),
                 colorRow(g.color, [this, inner](StyleColor c) {
                     (inner ? work_.innerGlow : work_.outerGlow).color = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Opacity:"),
                 percentRow(g.opacity * 100.0, [this, inner](double v) {
                     (inner ? work_.innerGlow : work_.outerGlow).opacity =
                         static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Spread:"),
                 percentRow(g.spread * 100.0, [this, inner](double v) {
                     (inner ? work_.innerGlow : work_.outerGlow).spread =
                         static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Size:"),
                 pixelRow(g.size, [this, inner](double v) {
                     (inner ? work_.innerGlow : work_.outerGlow).size =
                         static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    return page;
}

QWidget* LayerStyleDialog::buildSatinPage() {
    QFormLayout* form = nullptr;
    QScrollArea* page = formPage(&form, this);
    render::SatinStyle& s = work_.satin;

    form->addRow(tr("Blend Mode:"),
                 blendRow(s.blend, [this](StyleBlend m) {
                     work_.satin.blend = m;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Color:"),
                 colorRow(s.color, [this](StyleColor c) {
                     work_.satin.color = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Opacity:"),
                 percentRow(s.opacity * 100.0, [this](double v) {
                     work_.satin.opacity = static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Angle:"),
                 degreeRow(s.angle, [this](double v) {
                     work_.satin.angle = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Distance:"),
                 pixelRow(s.distance, [this](double v) {
                     work_.satin.distance = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Size:"),
                 pixelRow(s.size, [this](double v) {
                     work_.satin.size = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    auto* invert = new QCheckBox(tr("Invert"), page);
    invert->setChecked(s.invert);
    connect(invert, &QCheckBox::toggled, this, [this](bool on) {
        work_.satin.invert = on;
        changed_ = true;
        schedulePreview();
    });
    form->addRow(QString(), invert);
    return page;
}

QWidget* LayerStyleDialog::buildColorOverlayPage() {
    QFormLayout* form = nullptr;
    QScrollArea* page = formPage(&form, this);
    render::ColorOverlayStyle& o = work_.colorOverlay;

    form->addRow(tr("Blend Mode:"),
                 blendRow(o.blend, [this](StyleBlend m) {
                     work_.colorOverlay.blend = m;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Color:"),
                 colorRow(o.color, [this](StyleColor c) {
                     work_.colorOverlay.color = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Opacity:"),
                 percentRow(o.opacity * 100.0, [this](double v) {
                     work_.colorOverlay.opacity = static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    return page;
}

QWidget* LayerStyleDialog::buildGradientOverlayPage() {
    QFormLayout* form = nullptr;
    QScrollArea* page = formPage(&form, this);
    render::GradientStyle& g = work_.gradient;

    form->addRow(tr("Blend Mode:"),
                 blendRow(g.blend, [this](StyleBlend m) {
                     work_.gradient.blend = m;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Opacity:"),
                 percentRow(g.opacity * 100.0, [this](double v) {
                     work_.gradient.opacity = static_cast<float>(v / 100.0);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("From:"),
                 colorRow(g.from, [this](StyleColor c) {
                     work_.gradient.from = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("To:"),
                 colorRow(g.to, [this](StyleColor c) {
                     work_.gradient.to = c;
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Angle:"),
                 degreeRow(g.angle, [this](double v) {
                     work_.gradient.angle = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    form->addRow(tr("Scale:"),
                 new SliderSpin(0.1, 3.0, g.scale, 2, 0.05, QString(),
                                [this](double v) {
                                    work_.gradient.scale = static_cast<float>(v);
                                    changed_ = true;
                                    schedulePreview();
                                },
                                page));
    auto* radial = new QCheckBox(tr("Radial"), page);
    radial->setChecked(g.radial);
    connect(radial, &QCheckBox::toggled, this, [this](bool on) {
        work_.gradient.radial = on;
        changed_ = true;
        schedulePreview();
    });
    form->addRow(QString(), radial);
    auto* reverse = new QCheckBox(tr("Reverse"), page);
    reverse->setChecked(g.reverse);
    connect(reverse, &QCheckBox::toggled, this, [this](bool on) {
        work_.gradient.reverse = on;
        changed_ = true;
        schedulePreview();
    });
    form->addRow(QString(), reverse);
    return page;
}

QWidget* LayerStyleDialog::buildBlurPage() {
    QFormLayout* form = nullptr;
    QScrollArea* page = formPage(&form, this);
    render::BlurStyle& b = work_.blur;

    form->addRow(tr("Radius:"),
                 pixelRow(b.radius, [this](double v) {
                     work_.blur.radius = static_cast<float>(v);
                     changed_ = true;
                     schedulePreview();
                 }, page));
    auto* preserve = new QCheckBox(tr("Preserve Alpha"), page);
    preserve->setChecked(b.preserveAlpha);
    connect(preserve, &QCheckBox::toggled, this, [this](bool on) {
        work_.blur.preserveAlpha = on;
        changed_ = true;
        schedulePreview();
    });
    form->addRow(QString(), preserve);
    return page;
}

}  // namespace pittore::ui
