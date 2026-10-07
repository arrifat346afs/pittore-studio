#include "ui/tone_blend_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "ui/app_state.h"

namespace pittore::ui {
namespace {

// Slider + spin box acting as one control (mirrors the Layer Style dialog's
// conventional numeric field).
class SliderSpin final : public QWidget {
  public:
    SliderSpin(double min, double max, double value, int decimals, double step,
               const QString& suffix, std::function<void(double)> onChanged,
               QWidget* parent = nullptr)
        : QWidget(parent), onChanged_(std::move(onChanged)) {
        const double factor = std::pow(10.0, decimals);
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        slider_ = new QSlider(Qt::Horizontal, this);
        slider_->setRange(static_cast<int>(std::lround(min * factor)),
                          static_cast<int>(std::lround(max * factor)));
        slider_->setValue(static_cast<int>(std::lround(value * factor)));
        spin_ = new QDoubleSpinBox(this);
        spin_->setRange(min, max);
        spin_->setDecimals(decimals);
        spin_->setSingleStep(step);
        spin_->setSuffix(suffix);
        spin_->setValue(value);
        spin_->setFixedWidth(82);
        row->addWidget(slider_, 1);
        row->addWidget(spin_);
        connect(slider_, &QSlider::valueChanged, this, [this, factor](int v) {
            QSignalBlocker block(spin_);
            spin_->setValue(v / factor);
            onChanged_(v / factor);
        });
        connect(spin_, &QDoubleSpinBox::valueChanged, this,
                [this, factor](double v) {
                    QSignalBlocker block(slider_);
                    slider_->setValue(
                        static_cast<int>(std::lround(v * factor)));
                    onChanged_(v);
                });
    }

  private:
    QSlider* slider_ = nullptr;
    QDoubleSpinBox* spin_ = nullptr;
    std::function<void(double)> onChanged_;
};

SliderSpin* percentRow(double value, std::function<void(double)> f,
                       QWidget* parent) {
    return new SliderSpin(0.0, 100.0, value, 0, 1.0, QStringLiteral("%"),
                          std::move(f), parent);
}

SliderSpin* signedPercentRow(double value, std::function<void(double)> f,
                             QWidget* parent) {
    return new SliderSpin(-100.0, 100.0, value, 0, 1.0, QStringLiteral("%"),
                          std::move(f), parent);
}

}  // namespace

ToneBlendDialog::ToneBlendDialog(AppState* state, int groupIndex,
                                 QWidget* parent)
    : QDialog(parent), state_(state), groupIndex_(groupIndex) {
    setWindowTitle(tr("Tone Blend Group"));
    resize(420, 320);

    doc_ = state_->activeDocument();
    if (LayerItem* target = targetGroup()) {
        original_ = target->toneBlend;
        work_ = original_;
    }
    // One history step for the whole dialog session: the pre-dialog params
    // are snapshotted now, the live preview edits happen against them, and
    // OK promotes the step while Cancel restores and drops it.
    state_->beginUndoStep();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout;
    form->setSpacing(8);

    loading_ = true;
    form->addRow(tr("Blend Strength:"),
                 percentRow(work_.strength * 100.0,
                            [this](double v) {
                                work_.strength =
                                    std::clamp<float>(v / 100.0f, 0.0f, 1.0f);
                                changed_ = true;
                                schedulePreview();
                            },
                            this));
    form->addRow(tr("Blend Color:"),
                 percentRow(work_.color * 100.0,
                            [this](double v) {
                                work_.color =
                                    std::clamp<float>(v / 100.0f, 0.0f, 1.0f);
                                changed_ = true;
                                schedulePreview();
                            },
                            this));
    form->addRow(tr("Blend Contrast:"),
                 signedPercentRow(work_.contrast * 100.0,
                                  [this](double v) {
                                      work_.contrast = std::clamp<float>(
                                          v / 100.0f, -1.0f, 1.0f);
                                      changed_ = true;
                                      schedulePreview();
                                  },
                                  this));
    form->addRow(tr("Low Pass:"),
                 percentRow(work_.lowPass * 100.0,
                            [this](double v) {
                                work_.lowPass =
                                    std::clamp<float>(v / 100.0f, 0.0f, 1.0f);
                                changed_ = true;
                                schedulePreview();
                            },
                            this));
    auto* contentType = new QComboBox(this);
    contentType->addItems({tr("Image"), tr("Vector"), tr("Text")});
    contentType->setCurrentIndex(
        std::clamp(work_.contentType, 0, 2));
    connect(contentType, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                if (loading_) return;
                work_.contentType = std::clamp(index, 0, 2);
                changed_ = true;
                schedulePreview();
            });
    form->addRow(tr("Content Type:"), contentType);
    root->addLayout(form, 1);

    auto* hint = new QLabel(
        tr("Blends the group's layers against the composite beneath it. "
           "Lower Low Pass leaks backdrop detail; Text/Vector only change "
           "how children rasterize."),
        this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &ToneBlendDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this,
            &ToneBlendDialog::reject);
    root->addWidget(buttons);
    loading_ = false;
}

ToneBlendDialog::~ToneBlendDialog() = default;

void ToneBlendDialog::schedulePreview() {
    if (loading_) return;
    previewPending_ = true;
    if (!previewTimer_) {
        previewTimer_ = new QTimer(this);
        previewTimer_->setInterval(66);
        connect(previewTimer_, &QTimer::timeout, this, [this] {
            if (previewPending_) {
                previewPending_ = false;
                previewNow();
            } else {
                previewTimer_->stop();
            }
        });
    }
    if (!previewTimer_->isActive()) {
        previewPending_ = false;
        previewNow();
        previewTimer_->start();
    }
}

void ToneBlendDialog::previewNow() {
    LayerItem* group = targetGroup();
    if (!group || !doc_) return;
    group->toneBlend = work_;
    doc_->rebuildComposite();
    emit state_->documentModified(doc_);
}

LayerItem* ToneBlendDialog::targetGroup() const {
    if (!doc_ || groupIndex_ < 0 || groupIndex_ >= doc_->layers.size())
        return nullptr;
    LayerItem& layer = doc_->layers[groupIndex_];
    if (layer.kind != LayerItem::Kind::Group || !layer.toneBlendGroup)
        return nullptr;
    return &layer;
}

void ToneBlendDialog::accept() {
    if (previewTimer_) previewTimer_->stop();
    previewPending_ = false;
    LayerItem* group = targetGroup();
    if (!group || !doc_) {
        state_->discardUndoStep();
        QDialog::accept();
        return;
    }
    previewNow();
    if (changed_)
        state_->commitUndoStep(tr("Tone Blend Group"),
                               QStringLiteral("tone-blend"));
    else
        state_->discardUndoStep();
    QDialog::accept();
}

void ToneBlendDialog::reject() {
    if (previewTimer_) previewTimer_->stop();
    previewPending_ = false;
    LayerItem* group = targetGroup();
    if (group && doc_) {
        group->toneBlend = original_;
        doc_->rebuildComposite();
        emit state_->documentModified(doc_);
    }
    state_->discardUndoStep();
    QDialog::reject();
}

}  // namespace pittore::ui
