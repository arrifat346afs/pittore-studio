#include "ui/tone_dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

#include "engine/core/tonal_ops.h"
#include "ui/app_state.h"
#include "ui/curve_editor.h"
#include "ui/theme.h"

namespace pittore::ui {

namespace {

const char* kChannelItems[] = {"RGB", "Red", "Green", "Blue"};

// apply the active layer's current pixels through `fn` for a preview.
using ApplyFn = std::function<void(pittore::Image&)>;

// ---------------------------------------------------------------------------
// One live-edit session shared by the dialogs: first change opens an undo step
// + copy-on-write, previews recomposite without committing, Accept promotes the
// single history entry, Cancel restores the pre-edit pixels.
// Previews are interval-throttled (leading immediate + ~15fps while ticks
// arrive, trailing flush on accept): every preview re-applies from pristine,
// so a slider storm stays live without compounding or starving.
// ---------------------------------------------------------------------------
class ToneSession {
  public:
    ToneSession(AppState* state, QString name, QString iconKey, QObject* parent)
        : state_(state), name_(std::move(name)), icon_(std::move(iconKey)) {
        timer_ = new QTimer(parent);
        timer_->setInterval(66);
        QObject::connect(timer_, &QTimer::timeout, parent, [this] {
            if (pending_) {
                pending_ = false;
                applyPending();
            } else {
                timer_->stop();
            }
        });
    }

    bool ensure() {
        if (active_) return true;
        active_ = state_->beginTonalEdit() != nullptr;
        return active_;
    }
    bool active() const { return active_; }
    // Queue a preview of the latest values (leading edge applies now).
    void requestPreview(ApplyFn fn) {
        pendingFn_ = std::move(fn);
        if (!timer_->isActive()) {
            pending_ = false;
            applyPending();
            timer_->start();
        } else {
            pending_ = true;
        }
    }
    // Apply any queued preview now (accept path) and stand the timer down.
    void flushPreview() {
        timer_->stop();
        if (pending_) {
            pending_ = false;
            applyPending();
        }
    }
    void preview() { state_->applyTonalEditPreview(); }
    void accept() {
        flushPreview();
        if (active_) {
            state_->commitTonalEdit(name_, icon_);
            active_ = false;
        }
    }
    void reject() {
        timer_->stop();
        pending_ = false;
        if (active_) {
            state_->cancelTonalEdit();
            active_ = false;
        }
    }

  private:
    void applyPending() {
        if (!ensure()) return;
        state_->resetTonalEditPixels();
        LayerItem* layer = state_->activeLayer();
        if (!layer || !layer->pixels) return;
        pendingFn_(*layer->pixels);
        state_->applyTonalEditPreview();
    }

    AppState* state_;
    QString name_;
    QString icon_;
    bool active_ = false;
    QTimer* timer_ = nullptr;
    ApplyFn pendingFn_ = nullptr;
    bool pending_ = false;
};

void applyPreview(AppState* state, ToneSession& session, const ApplyFn& fn) {
    if (!session.ensure()) {
        state->setStatusHint(QObject::tr("Open a document with an editable pixel layer first."));
        return;
    }
    // Throttled + always from pristine (ToneSession restores first): every
    // preview reflects the current control values, never the last preview.
    session.requestPreview(fn);
}

}  // namespace

// ---------------------------------------------------------------------------
// Levels (R54): black / white / gamma with channel scoping and live preview.
// ---------------------------------------------------------------------------
class LevelsDialog::Impl {
  public:
    Impl(LevelsDialog* owner, AppState* state) : owner_(owner), state_(state),
        session_(state, QObject::tr("Levels"), QStringLiteral("levels"), owner_) {
        auto* form = new QFormLayout;
        channel_ = new QComboBox;
        for (const char* item : kChannelItems) channel_->addItem(QObject::tr(item));
        connect(channel_, &QComboBox::currentIndexChanged, owner, [this] { preview(); });
        form->addRow(QObject::tr("Channel:"), channel_);

        inBlack_ = makeSpin(0, 255, 0);
        inWhite_ = makeSpin(0, 255, 255);
        gamma_ = makeDouble(0.10, 9.99, 1.00, 2);
        outBlack_ = makeSpin(0, 255, 0);
        outWhite_ = makeSpin(0, 255, 255);
        form->addRow(QObject::tr("Black point:"), inBlack_);
        form->addRow(QObject::tr("White point:"), inWhite_);
        form->addRow(QObject::tr("Gamma:"), gamma_);
        form->addRow(QObject::tr("Output black:"), outBlack_);
        form->addRow(QObject::tr("Output white:"), outWhite_);

        auto* reset = new QPushButton(QObject::tr("Default"));
        connect(reset, &QPushButton::clicked, owner, [this] {
            QSignalBlocker b1(inBlack_), b2(inWhite_), b3(gamma_), b4(outBlack_), b5(outWhite_);
            inBlack_->setValue(0);
            inWhite_->setValue(255);
            gamma_->setValue(1.00);
            outBlack_->setValue(0);
            outWhite_->setValue(255);
            preview();
        });

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, owner, [this] {
            accept();
            owner_->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, owner, [this] {
            reject();
            owner_->reject();
        });

        auto* layout = new QVBoxLayout(owner);
        layout->addLayout(form);
        layout->addWidget(reset);
        layout->addWidget(buttons);
    }

    void preview() {
        applyPreview(state_, session_, [this](pittore::Image& img) {
            const int ch = channel_->currentIndex();  // 0 RGB, 1 R, 2 G, 3 B
            pittore::applyLevels(img, ch, inBlack_->value() / 255.0,
                                  inWhite_->value() / 255.0, gamma_->value(),
                                  outBlack_->value() / 255.0, outWhite_->value() / 255.0);
        });
    }
    void accept() { session_.accept(); }
    void reject() { session_.reject(); }

  private:
    QSpinBox* makeSpin(int min, int max, int value) {
        auto* s = new QSpinBox;
        s->setRange(min, max);
        s->setValue(value);
        connect(s, qOverload<int>(&QSpinBox::valueChanged), owner_, [this] { preview(); });
        return s;
    }
    QDoubleSpinBox* makeDouble(double min, double max, double value, int decimals) {
        auto* s = new QDoubleSpinBox;
        s->setRange(min, max);
        s->setDecimals(decimals);
        s->setSingleStep(0.01);
        s->setValue(value);
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), owner_, [this] { preview(); });
        return s;
    }

    LevelsDialog* owner_;
    AppState* state_;
    ToneSession session_;
    QComboBox* channel_;
    QSpinBox* inBlack_;
    QSpinBox* inWhite_;
    QDoubleSpinBox* gamma_;
    QSpinBox* outBlack_;
    QSpinBox* outWhite_;
};

LevelsDialog::LevelsDialog(AppState* state, QWidget* parent)
    : QDialog(parent), impl_(new Impl(this, state)) {
    setWindowTitle(tr("Levels"));
    setModal(true);
}

void LevelsDialog::preview() { impl_->preview(); }

// Curves (R54): the shared CurveEditor surface with a live preview.

class CurvesDialog::Impl {
  public:
    Impl(CurvesDialog* owner, AppState* state) : owner_(owner), state_(state),
        session_(state, QObject::tr("Curves"), QStringLiteral("curves"), owner_) {
        channel_ = new QComboBox;
        for (const char* item : kChannelItems) channel_->addItem(QObject::tr(item));
        connect(channel_, &QComboBox::currentIndexChanged, owner, [this] { preview(); });

        auto* reset = new QPushButton(QObject::tr("Reset"));
        connect(reset, &QPushButton::clicked, owner, [this] { curve_->reset(); });

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, owner, [this] {
            accept();
            owner_->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, owner, [this] {
            reject();
            owner_->reject();
        });

        auto* top = new QHBoxLayout;
        top->addWidget(new QLabel(QObject::tr("Channel:")));
        top->addWidget(channel_);
        top->addStretch(1);
        top->addWidget(reset);

        auto* layout = new QVBoxLayout(owner);
        layout->addLayout(top);
        curve_ = new CurveEditor;
        curve_->onChanged_ = [this] { preview(); };
        layout->addWidget(curve_);
        layout->addWidget(buttons);
    }

    void preview() {
        applyPreview(state_, session_, [this](pittore::Image& img) {
            std::vector<std::pair<double, double>> pts;
            pts.reserve(curve_->points().size());
            for (const QPointF& p : curve_->points())
                pts.emplace_back(p.x(), p.y());
            pittore::applyCurves(img, channel_->currentIndex(), pts);
        });
    }
    void accept() { session_.accept(); }
    void reject() { session_.reject(); }

  private:
    CurvesDialog* owner_;
    AppState* state_;
    ToneSession session_;
    CurveEditor* curve_ = nullptr;
    QComboBox* channel_;
};

CurvesDialog::CurvesDialog(AppState* state, QWidget* parent)
    : QDialog(parent), impl_(new Impl(this, state)) {
    setWindowTitle(tr("Curves"));
    setModal(true);
}

CurvesDialog::~CurvesDialog() { delete impl_; }

void CurvesDialog::preview() { impl_->preview(); }

// ---------------------------------------------------------------------------
// Add Noise (R49)
// ---------------------------------------------------------------------------
class AddNoiseDialog::Impl {
  public:
    Impl(AddNoiseDialog* owner, AppState* state) : owner_(owner), state_(state),
        session_(state, QObject::tr("Add Noise"), QStringLiteral("noise"), owner_) {
        amount_ = new QSlider(Qt::Horizontal);
        amount_->setRange(0, 100);
        amount_->setValue(12);
        amount_->setFixedWidth(180);
        connect(amount_, &QSlider::valueChanged, owner, [this] { preview(); });
        mono_ = new QCheckBox(QObject::tr("Monochromatic"));
        mono_->setChecked(true);
        connect(mono_, &QCheckBox::toggled, owner, [this] { preview(); });

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, owner, [this] {
            accept();
            owner_->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, owner, [this] {
            reject();
            owner_->reject();
        });

        auto* form = new QFormLayout;
        form->addRow(QObject::tr("Amount:"), amount_);
        form->addRow(QString(), mono_);
        auto* layout = new QVBoxLayout(owner);
        layout->addLayout(form);
        layout->addWidget(buttons);
    }

    void preview() {
        // A fixed seed keeps the preview identical to the accepted result.
        applyPreview(state_, session_, [this](pittore::Image& img) {
            pittore::applyAddNoise(img, amount_->value() / 100.0, mono_->isChecked(), 0xC0FFEEULL);
        });
    }
    void accept() { session_.accept(); }
    void reject() { session_.reject(); }

  private:
    AddNoiseDialog* owner_;
    AppState* state_;
    ToneSession session_;
    QSlider* amount_;
    QCheckBox* mono_;
};

AddNoiseDialog::AddNoiseDialog(AppState* state, QWidget* parent)
    : QDialog(parent), impl_(new Impl(this, state)) {
    setWindowTitle(tr("Add Noise"));
    setModal(true);
}

void AddNoiseDialog::preview() { impl_->preview(); }

// ---------------------------------------------------------------------------
// Median (R49)
// ---------------------------------------------------------------------------
class MedianDialog::Impl {
  public:
    Impl(MedianDialog* owner, AppState* state) : owner_(owner), state_(state),
        session_(state, QObject::tr("Median"), QStringLiteral("median"), owner_) {
        radius_ = new QSpinBox;
        radius_->setRange(1, 100);
        radius_->setValue(1);
        connect(radius_, qOverload<int>(&QSpinBox::valueChanged), owner, [this] { preview(); });

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, owner, [this] {
            accept();
            owner_->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, owner, [this] {
            reject();
            owner_->reject();
        });

        auto* form = new QFormLayout;
        form->addRow(QObject::tr("Radius:"), radius_);
        auto* layout = new QVBoxLayout(owner);
        layout->addLayout(form);
        layout->addWidget(buttons);
    }

    void preview() {
        applyPreview(state_, session_, [this](pittore::Image& img) {
            pittore::applyMedianFilter(img, radius_->value());
        });
    }
    void accept() { session_.accept(); }
    void reject() { session_.reject(); }

  private:
    MedianDialog* owner_;
    AppState* state_;
    ToneSession session_;
    QSpinBox* radius_;
};

MedianDialog::MedianDialog(AppState* state, QWidget* parent)
    : QDialog(parent), impl_(new Impl(this, state)) {
    setWindowTitle(tr("Median"));
    setModal(true);
}

void MedianDialog::preview() { impl_->preview(); }

// ---------------------------------------------------------------------------
// Unsharp Mask (R49)
// ---------------------------------------------------------------------------
class UnsharpMaskDialog::Impl {
  public:
    Impl(UnsharpMaskDialog* owner, AppState* state) : owner_(owner), state_(state),
        session_(state, QObject::tr("Unsharp Mask"), QStringLiteral("sharpen"), owner_) {
        amount_ = new QSpinBox;
        amount_->setRange(1, 500);
        amount_->setValue(100);
        amount_->setSuffix(QObject::tr(" %"));
        connect(amount_, qOverload<int>(&QSpinBox::valueChanged), owner, [this] { preview(); });
        radius_ = new QDoubleSpinBox;
        radius_->setRange(0.1, 250.0);
        radius_->setSingleStep(0.1);
        radius_->setValue(1.0);
        connect(radius_, qOverload<double>(&QDoubleSpinBox::valueChanged), owner,
                [this] { preview(); });
        threshold_ = new QSpinBox;
        threshold_->setRange(0, 255);
        threshold_->setValue(0);
        connect(threshold_, qOverload<int>(&QSpinBox::valueChanged), owner, [this] { preview(); });

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, owner, [this] {
            accept();
            owner_->accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, owner, [this] {
            reject();
            owner_->reject();
        });

        auto* form = new QFormLayout;
        form->addRow(QObject::tr("Amount:"), amount_);
        form->addRow(QObject::tr("Radius:"), radius_);
        form->addRow(QObject::tr("Threshold:"), threshold_);
        auto* layout = new QVBoxLayout(owner);
        layout->addLayout(form);
        layout->addWidget(buttons);
    }

    void preview() {
        applyPreview(state_, session_, [this](pittore::Image& img) {
            pittore::applyUnsharpMask(img, amount_->value() / 100.0,
                                       std::max(1, static_cast<int>(std::lround(radius_->value()))),
                                       threshold_->value() / 255.0);
        });
    }
    void accept() { session_.accept(); }
    void reject() { session_.reject(); }

  private:
    UnsharpMaskDialog* owner_;
    AppState* state_;
    ToneSession session_;
    QSpinBox* amount_;
    QDoubleSpinBox* radius_;
    QSpinBox* threshold_;
};

UnsharpMaskDialog::UnsharpMaskDialog(AppState* state, QWidget* parent)
    : QDialog(parent), impl_(new Impl(this, state)) {
    setWindowTitle(tr("Unsharp Mask"));
    setModal(true);
}

void UnsharpMaskDialog::preview() { impl_->preview(); }

}  // namespace pittore::ui