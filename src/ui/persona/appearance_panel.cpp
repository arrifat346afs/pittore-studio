#include "ui/persona/appearance_panel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "ui/app_state.h"
#include "ui/panels/registry/panel_creators.h"
#include "ui/persona/vector_edit.h"

namespace pittore::ui {
namespace {

QColor paintColor(const std::uint8_t rgba[4]) {
    return QColor(rgba[0], rgba[1], rgba[2], rgba[3]);
}

void setPaintColor(std::uint8_t rgba[4], const QColor& c) {
    rgba[0] = static_cast<std::uint8_t>(c.red());
    rgba[1] = static_cast<std::uint8_t>(c.green());
    rgba[2] = static_cast<std::uint8_t>(c.blue());
    rgba[3] = static_cast<std::uint8_t>(c.alpha());
}

void styleColorButton(QPushButton* button, const QColor& c) {
    button->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #888;")
                              .arg(c.name(QColor::HexArgb)));
    button->setText(c.name(QColor::HexRgb).toUpper());
}

}  // namespace

AppearancePanel::AppearancePanel(AppState* state, QWidget* parent)
    : QWidget(parent), state_(state) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(8);

    title_ = new QLabel(tr("No vector layer selected"), this);
    title_->setWordWrap(true);
    outer->addWidget(title_);

    auto addRow = [this, outer](const QString& label, QWidget* left, QWidget* right) {
        auto* row = new QWidget(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto* tag = new QLabel(label, row);
        tag->setFixedWidth(48);
        layout->addWidget(tag);
        layout->addWidget(left);
        layout->addWidget(right, 1);
        outer->addWidget(row);
    };

    fillCheck_ = new QCheckBox(this);
    fillButton_ = new QPushButton(this);
    fillButton_->setCursor(Qt::PointingHandCursor);
    addRow(tr("Fill"), fillCheck_, fillButton_);

    strokeCheck_ = new QCheckBox(this);
    strokeButton_ = new QPushButton(this);
    strokeButton_->setCursor(Qt::PointingHandCursor);
    addRow(tr("Stroke"), strokeCheck_, strokeButton_);

    widthSpin_ = new QDoubleSpinBox(this);
    widthSpin_->setRange(0.01, 1000.0);
    widthSpin_->setSingleStep(0.5);
    widthSpin_->setDecimals(2);
    widthSpin_->setSuffix(tr(" px"));
    widthSpin_->setKeyboardTracking(false);
    auto* widthTag = new QLabel(tr("Width"), this);
    widthTag->setFixedWidth(48);
    auto* widthRow = new QWidget(this);
    auto* widthLayout = new QHBoxLayout(widthRow);
    widthLayout->setContentsMargins(0, 0, 0, 0);
    widthLayout->addWidget(widthTag);
    widthLayout->addWidget(widthSpin_, 1);
    outer->addWidget(widthRow);

    opacitySlider_ = new QSlider(Qt::Horizontal, this);
    opacitySlider_->setRange(1, 100);
    opacityLabel_ = new QLabel(QStringLiteral("100%"), this);
    auto* opacityTag = new QLabel(tr("Opacity"), this);
    opacityTag->setFixedWidth(48);
    auto* opacityRow = new QWidget(this);
    auto* opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(opacityTag);
    opacityLayout->addWidget(opacitySlider_, 1);
    opacityLayout->addWidget(opacityLabel_);
    outer->addWidget(opacityRow);

    auto* addRow2 = new QWidget(this);
    auto* addLayout = new QHBoxLayout(addRow2);
    addLayout->setContentsMargins(0, 0, 0, 0);
    addStrokeButton_ = new QPushButton(tr("Add Stroke"), addRow2);
    addFillButton_ = new QPushButton(tr("Add Fill"), addRow2);
    // Multi-fill needs the ArtNodeList model (Phase 2b): visible, inert.
    addStrokeButton_->setEnabled(false);
    addFillButton_->setEnabled(false);
    addStrokeButton_->setToolTip(tr("Multiple strokes need stacked fills — planned."));
    addFillButton_->setToolTip(tr("Multiple fills need stacked fills — planned."));
    addLayout->addWidget(addStrokeButton_);
    addLayout->addWidget(addFillButton_);
    outer->addWidget(addRow2);
    outer->addStretch(1);

    // One undo step per discrete gesture (never per slider tick).
    connect(fillButton_, &QPushButton::clicked, this, [this] { pickColor(true); });
    connect(strokeButton_, &QPushButton::clicked, this, [this] { pickColor(false); });
    connect(fillCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (layerIndex_ < 0) return;
        DocumentItem* d = state_->activeDocument();
        if (!d || !d->layers[layerIndex_].art) return;
        auto paint = d->layers[layerIndex_].art->paint;
        paint.hasFill = on;
        if (!on && !paint.hasStroke) {
            paint.hasStroke = true;
            const QSignalBlocker block(strokeCheck_);
            strokeCheck_->setChecked(true);
        }
        commit(paint, d->layers[layerIndex_].art->opacity, tr("Fill"));
    });
    connect(strokeCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (layerIndex_ < 0) return;
        DocumentItem* d = state_->activeDocument();
        if (!d || !d->layers[layerIndex_].art) return;
        auto paint = d->layers[layerIndex_].art->paint;
        paint.hasStroke = on;
        if (!on && !paint.hasFill) {
            paint.hasFill = true;
            const QSignalBlocker block(fillCheck_);
            fillCheck_->setChecked(true);
        }
        commit(paint, d->layers[layerIndex_].art->opacity, tr("Stroke"));
    });
    connect(widthSpin_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (layerIndex_ < 0) return;
        DocumentItem* d = state_->activeDocument();
        if (!d || !d->layers[layerIndex_].art) return;
        auto paint = d->layers[layerIndex_].art->paint;
        paint.strokeWidth = widthSpin_->value();
        commit(paint, d->layers[layerIndex_].art->opacity, tr("Stroke Width"));
    });
    connect(opacitySlider_, &QSlider::valueChanged, this, [this](int v) {
        opacityLabel_->setText(QStringLiteral("%1%").arg(v));
    });
    connect(opacitySlider_, &QSlider::sliderReleased, this, [this] {
        if (layerIndex_ < 0) return;
        DocumentItem* d = state_->activeDocument();
        if (!d || !d->layers[layerIndex_].art) return;
        commit(d->layers[layerIndex_].art->paint,
               opacitySlider_->value() / 100.0, tr("Opacity"));
    });

    connect(state_, &AppState::layersChanged, this, [this] { rebuild(); });
    connect(state_, &AppState::historyChanged, this, [this] { rebuild(); });
    connect(state_, &AppState::activeLayerChanged, this, [this] { rebuild(); });
    connect(state_, &AppState::activeDocumentChanged, this, [this] { rebuild(); });

    rebuild();
}

void AppearancePanel::commit(const pittore::vector::ArtPaint& paint, double opacity,
                             const QString& undoName) {
    if (layerIndex_ < 0) return;
    state_->applyVectorPaint(layerIndex_, paint, opacity, undoName);
}

void AppearancePanel::pickColor(bool fill) {
    if (layerIndex_ < 0) return;
    DocumentItem* d = state_->activeDocument();
    if (!d || !d->layers[layerIndex_].art) return;
    const auto& paint = d->layers[layerIndex_].art->paint;
    const QColor start = paintColor(fill ? paint.fill : paint.stroke);
    const QColor picked = QColorDialog::getColor(
        start, this, fill ? tr("Fill Color") : tr("Stroke Color"),
        QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return;
    auto next = paint;
    if (fill) {
        next.hasFill = true;
        setPaintColor(next.fill, picked);
        const QSignalBlocker block(fillCheck_);
        fillCheck_->setChecked(true);
    } else {
        next.hasStroke = true;
        setPaintColor(next.stroke, picked);
        const QSignalBlocker block(strokeCheck_);
        strokeCheck_->setChecked(true);
    }
    commit(next, d->layers[layerIndex_].art->opacity,
           fill ? tr("Fill Color") : tr("Stroke Color"));
}

void AppearancePanel::rebuild() {
    layerIndex_ = vectorEditableLayer(state_);
    DocumentItem* d = state_->activeDocument();
    const pittore::vector::ArtNode* art =
        (d && layerIndex_ >= 0) ? d->layers[layerIndex_].art.get() : nullptr;

    const QSignalBlocker b0(fillCheck_), b1(strokeCheck_), b2(widthSpin_),
        b3(opacitySlider_);
    const bool enabled = art != nullptr;
    title_->setText(art ? d->layers[layerIndex_].name
                        : tr("No vector layer selected"));
    fillCheck_->setEnabled(enabled);
    fillButton_->setEnabled(enabled);
    strokeCheck_->setEnabled(enabled);
    strokeButton_->setEnabled(enabled);
    widthSpin_->setEnabled(enabled);
    opacitySlider_->setEnabled(enabled);
    if (!art) return;

    const auto& paint = art->paint;
    fillCheck_->setChecked(paint.hasFill);
    strokeCheck_->setChecked(paint.hasStroke);
    styleColorButton(fillButton_, paintColor(paint.fill));
    styleColorButton(strokeButton_, paintColor(paint.stroke));
    widthSpin_->setValue(paint.strokeWidth);
    const int pct = qRound(qBound(0.0, art->opacity, 1.0) * 100.0);
    opacitySlider_->setValue(pct);
    opacityLabel_->setText(QStringLiteral("%1%").arg(pct));
}

QWidget* createAppearancePanel(AppState* state, QWidget* parent) {
    return new AppearancePanel(state, parent);
}

}  // namespace pittore::ui
