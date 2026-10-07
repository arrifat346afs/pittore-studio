#include "ui/filter_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QVector>
#include <QWheelEvent>

#include <cmath>
#include <functional>

#include "engine/filter/filters.h"
#include "engine/core/log.h"
#include "ui/app_state.h"
#include "ui/icons.h"
#include "ui/theme.h"

namespace pittore::ui {

class FilterSession {
public:
    FilterSession(AppState *state, const QString &name, const QString &icon)
        : state_(state), name_(name), icon_(icon) {}
    bool ensure() {
        if (active_) return true;
        active_ = state_->beginTonalEdit() != nullptr;
        return active_;
    }
    void preview() { state_->applyTonalEditPreview(); }
    void accept() {
        if (active_) {
            state_->commitTonalEdit(name_, icon_);
            active_ = false;
        }
    }
    void reject() {
        if (active_) {
            state_->cancelTonalEdit();
            active_ = false;
        }
    }

private:
    AppState *state_;
    QString name_;
    QString icon_;
    bool active_ = false;
};

FilterPreviewCanvas::FilterPreviewCanvas(AppState *state, QWidget *parent)
    : QWidget(parent), state_(state) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 240);
    setCursor(Qt::OpenHandCursor);
}

void FilterPreviewCanvas::setTool(int tool) {
    tool_ = tool;
    setCursor(tool_ == 1 ? Qt::CrossCursor : Qt::OpenHandCursor);
    update();
}

double FilterPreviewCanvas::zoom() const { return zoom_; }

void FilterPreviewCanvas::setZoomIndex(int index) {
    zoomIndex_ = std::clamp(index, 0, 4);
    if (zoomIndex_ < 4) {
        static const double presets[4] = {0.25, 0.5, 1.0, 2.0};
        applyZoom(presets[zoomIndex_], QPointF(width() * 0.5, height() * 0.5));
    } else {
        zoomToFit();
    }
}

void FilterPreviewCanvas::applyZoom(double z, QPointF anchor) {
    const double nz = std::clamp(z, 0.05, 32.0);
    if (anchor.x() >= 0 && nz != zoom_) {
        const QPointF before = (anchor - pan_) / zoom_;
        pan_ = anchor - before * nz;
    }
    if (nz != zoom_) {
        zoom_ = nz;
        zoomIndex_ = -1;
        update();
        emit zoomChanged();
    }
}

void FilterPreviewCanvas::zoomToFit() {
    if (image_.isNull()) return;
    double ox = 0.0, oy = 0.0, dw = image_.width(), dh = image_.height();
    if (LayerItem *l = state_->activeLayer()) {
        if (l->pixels && l->pixels->width() ==
                                 static_cast<std::uint32_t>(image_.width()) &&
            l->pixels->height() ==
                static_cast<std::uint32_t>(image_.height())) {
            ox = l->offset.x();
            oy = l->offset.y();
            dw = l->pixels->width() * l->scaleX;
            dh = l->pixels->height() * l->scaleY;
        }
    }
    const double zx = width() / std::max(1.0, dw);
    const double zy = height() / std::max(1.0, dh);
    zoom_ = std::clamp(std::min(zx, zy), 0.05, 32.0);
    pan_ = QPointF(width() * 0.5 - (ox + dw * 0.5) * zoom_,
                   height() * 0.5 - (oy + dh * 0.5) * zoom_);
    zoomIndex_ = 4;
    update();
    emit zoomChanged();
}

void FilterPreviewCanvas::zoomToWidth() {
    if (image_.isNull() || image_.width() <= 0) return;
    double ox = 0.0, dw = image_.width();
    double oy = 0.0, dh = image_.height();
    if (LayerItem *l = state_->activeLayer()) {
        if (l->pixels && l->pixels->width() ==
                                 static_cast<std::uint32_t>(image_.width()) &&
            l->pixels->height() ==
                static_cast<std::uint32_t>(image_.height())) {
            ox = l->offset.x();
            oy = l->offset.y();
            dw = l->pixels->width() * l->scaleX;
            dh = l->pixels->height() * l->scaleY;
        }
    }
    zoom_ = std::clamp(width() / std::max(1.0, dw), 0.05, 32.0);
    pan_ = QPointF(width() * 0.5 - (ox + dw * 0.5) * zoom_,
                   height() * 0.5 - (oy + dh * 0.5) * zoom_);
    zoomIndex_ = -1;
    update();
    emit zoomChanged();
}

QPointF FilterPreviewCanvas::viewToImage(QPointF vp) const {
    return QPointF((vp.x() - pan_.x()) / zoom_, (vp.y() - pan_.y()) / zoom_);
}

QPointF FilterPreviewCanvas::imageToView(QPointF ip) const {
    return QPointF(ip.x() * zoom_ + pan_.x(), ip.y() * zoom_ + pan_.y());
}

void FilterPreviewCanvas::setPreviewImage(const QImage &image) {
    preview_ = image;
    image_ = image;
    update();
}

void FilterPreviewCanvas::clearPreviewImage() {
    preview_ = QImage();
}

void FilterPreviewCanvas::refresh() {
    if (!preview_.isNull()) {
        image_ = preview_;
        update();
        return;
    }
    LayerItem *layer = state_->activeLayer();
    if (!layer || !layer->pixels || layer->pixels->width() == 0 ||
        layer->pixels->height() == 0) {
        image_ = QImage();
        update();
        return;
    }
    const std::uint32_t w = layer->pixels->width();
    const std::uint32_t h = layer->pixels->height();
    QImage out(int(w), int(h), QImage::Format_ARGB32_Premultiplied);
    for (std::uint32_t y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(int(y)));
        for (std::uint32_t x = 0; x < w; ++x) {
            const pittore::RGBAf &p = layer->pixels->at(x, y);
            const float a = std::clamp(p.a, 0.0f, 1.0f);
            row[x] = qRgba(int(std::clamp(p.r, 0.0f, 1.0f) * a * 255.0f),
                           int(std::clamp(p.g, 0.0f, 1.0f) * a * 255.0f),
                           int(std::clamp(p.b, 0.0f, 1.0f) * a * 255.0f),
                           int(a * 255.0f));
        }
    }
    const bool first = image_.isNull() || image_.size() != out.size();
    image_ = out;
    if (first && zoomIndex_ == 4) zoomToFit();
    else update();
}

void FilterPreviewCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), colorsFor(state_->theme()).surround);
    if (image_.isNull()) return;
    double ox = 0.0, oy = 0.0, sx = 1.0, sy = 1.0;
    if (LayerItem *l = state_->activeLayer()) {
        if (l->pixels && l->pixels->width() ==
                                 static_cast<std::uint32_t>(image_.width()) &&
            l->pixels->height() ==
                static_cast<std::uint32_t>(image_.height())) {
            ox = l->offset.x();
            oy = l->offset.y();
            sx = l->scaleX;
            sy = l->scaleY;
        }
    }
    const QRectF target(QPointF((ox) * zoom_ + pan_.x(), (oy) * zoom_ + pan_.y()),
                        QSizeF(image_.width() * sx * zoom_, image_.height() * sy * zoom_));
    const int cell = std::max(6, int(12 * std::min(zoom_, 2.0)));
    p.save();
    p.setClipRect(target);
    p.fillRect(target, QColor(128, 128, 128));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(160, 160, 160));
    const int x0 = int(target.left()) / cell * cell;
    const int y0 = int(target.top()) / cell * cell;
    for (int y = y0; y < target.bottom(); y += cell) {
        for (int x = x0; x < target.right(); x += cell) {
            if (((x / cell) + (y / cell)) % 2 == 0)
                p.drawRect(x, y, cell, cell);
        }
    }
    p.restore();
    p.drawImage(target, image_);
}

void FilterPreviewCanvas::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::MiddleButton || tool_ == 0 || spaceHeld_) {
        panning_ = true;
        pressPos_ = event->pos();
        pressPan_ = pan_;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::LeftButton && tool_ == 1) {
        const double f = event->modifiers().testFlag(Qt::AltModifier) ? 0.8 : 1.25;
        applyZoom(zoom_ * f, event->position());
    }
}

void FilterPreviewCanvas::mouseMoveEvent(QMouseEvent *event) {
    if (panning_) {
        pan_ = pressPan_ + (event->pos() - pressPos_);
        update();
    }
}

void FilterPreviewCanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton || panning_) {
        panning_ = false;
        setCursor(tool_ == 1 ? Qt::CrossCursor : Qt::OpenHandCursor);
    }
}

void FilterPreviewCanvas::wheelEvent(QWheelEvent *event) {
    applyZoom(zoom_ * std::pow(1.0015, event->angleDelta().y()), event->position());
    event->accept();
}

void FilterPreviewCanvas::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    QAction *zoomInAction = menu.addAction(tr("Zoom In"));
    QAction *zoomOutAction = menu.addAction(tr("Zoom Out"));
    menu.addSeparator();
    QAction *fitAction = menu.addAction(tr("Fit on Screen"));
    QAction *fitWidthAction = menu.addAction(tr("Fit to Width"));
    menu.addSeparator();
    QAction *pct50 = menu.addAction(tr("50%"));
    QAction *pct100 = menu.addAction(tr("100%"));
    QAction *pct150 = menu.addAction(tr("150%"));
    QAction *pct200 = menu.addAction(tr("200%"));
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen) return;
    if (chosen == zoomInAction)
        applyZoom(zoom_ * 1.25, mapFromGlobal(event->globalPos()));
    else if (chosen == zoomOutAction)
        applyZoom(zoom_ * 0.8, mapFromGlobal(event->globalPos()));
    else if (chosen == fitAction)
        zoomToFit();
    else if (chosen == fitWidthAction)
        zoomToWidth();
    else if (chosen == pct50)
        applyZoom(0.5, QPointF(width() * 0.5, height() * 0.5));
    else if (chosen == pct100)
        applyZoom(1.0, QPointF(width() * 0.5, height() * 0.5));
    else if (chosen == pct150)
        applyZoom(1.5, QPointF(width() * 0.5, height() * 0.5));
    else if (chosen == pct200)
        applyZoom(2.0, QPointF(width() * 0.5, height() * 0.5));
}

void FilterPreviewCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !spaceHeld_) {
        spaceHeld_ = true;
        setCursor(Qt::OpenHandCursor);
        return;
    }
    QWidget::keyPressEvent(event);
}

void FilterPreviewCanvas::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && spaceHeld_) {
        spaceHeld_ = false;
        setCursor(tool_ == 1 ? Qt::CrossCursor : Qt::OpenHandCursor);
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void FilterPreviewCanvas::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (zoomIndex_ == 4) zoomToFit();
}

class FilterDialog::Impl {
public:
    struct Row {
        int kind = 0;
        QSlider *slider = nullptr;
        QDoubleSpinBox *spin = nullptr;
        QComboBox *combo = nullptr;
        QCheckBox *check = nullptr;
        double min = 0.0;
        double max = 100.0;
    };

    Impl(FilterDialog *owner, AppState *state, const QString &filterId)
        : owner_(owner), state_(state), id_(filterId),
          session_(state, QString::fromStdString(filter::findFilter(filterId.toStdString()) ?
                      filter::findFilter(filterId.toStdString())->name : filterId.toStdString()),
                   QStringLiteral("filter")) {
        const filter::FilterDef *def = filter::findFilter(filterId.toStdString());
        canvas_ = new FilterPreviewCanvas(state_, owner);
        auto *rail = new QWidget(owner);
        rail->setFixedWidth(44);
        auto *railLayout = new QVBoxLayout(rail);
        railLayout->setContentsMargins(4, 8, 4, 8);
        railLayout->setSpacing(4);
        const ThemeColors tc = colorsFor(state_->theme());
        auto *toolGroup = new QButtonGroup(rail);
        toolGroup->setExclusive(true);
        const struct RailTool { const char *tip; const char *icon; const char *key; int tool; } railTools[2] = {
            {"Hand (H)", "hand", "H", 0},
            {"Zoom (Z)", "zoom", "Z", 1},
        };
        for (const auto &rt : railTools) {
            auto *b = new QToolButton(rail);
            b->setIcon(chromeIcon(QString::fromLatin1(rt.icon), tc.text, tc.accentText));
            b->setIconSize(QSize(22, 22));
            b->setFixedSize(36, 36);
            b->setCheckable(true);
            b->setAutoRaise(true);
            b->setToolTip(QString::fromLatin1(rt.tip));
            toolGroup->addButton(b, rt.tool);
            railLayout->addWidget(b);
            if (rt.tool == 0) b->setChecked(true);
            connect(b, &QToolButton::clicked, owner_, [this, rt] { canvas_->setTool(rt.tool); });
            auto *sc = new QShortcut(QKeySequence(QString::fromLatin1(rt.key)), owner_);
            connect(sc, &QShortcut::activated, owner_, [this, b] { b->click(); });
        }
        railLayout->addStretch(1);
        auto *props = new QWidget(owner);
        props->setFixedWidth(344);
        auto *propsLayout = new QVBoxLayout(props);
        propsLayout->setContentsMargins(0, 0, 0, 0);
        propsLayout->setSpacing(10);
        if (def && !def->params.empty()) {
            const QVector<QPair<QString, QVector<int>>> sections = sectionsFor(filterId);
            if (sections.size() > 1) {
                auto *tabs = new QTabWidget;
                for (const auto &sec : sections) {
                    auto *page = new QWidget;
                    auto *form = new QFormLayout(page);
                    form->setContentsMargins(16, 16, 16, 16);
                    for (int idx : sec.second) {
                        if (idx >= 0 && idx < static_cast<int>(def->params.size()))
                            addParamRow(form, def->params[static_cast<std::size_t>(idx)]);
                    }
                    tabs->addTab(page, sec.first);
                }
                propsLayout->addWidget(tabs, 1);
            } else {
                auto *group = new QGroupBox(QString::fromUtf8(def->name), owner);
                auto *form = new QFormLayout(group);
                for (const auto &p : def->params) addParamRow(form, p);
                propsLayout->addWidget(group, 1);
            }
        } else {
            auto *note = new QLabel(
                tr("This filter has no adjustable settings. The canvas shows a live preview — OK applies it, Cancel discards it."),
                owner);
            note->setWordWrap(true);
            note->setStyleSheet(QStringLiteral("color: %1;").arg(colorsFor(state_->theme()).textDim.name()));
            note->setParent(props);
            propsLayout->addWidget(note, 1);
        }
        propsLayout->addStretch(1);
        auto *middle = new QSplitter(Qt::Horizontal, owner);
        middle->addWidget(rail);
        middle->addWidget(canvas_);
        middle->addWidget(props);
        middle->setStretchFactor(0, 0);
        middle->setStretchFactor(1, 1);
        middle->setStretchFactor(2, 0);
        auto *bottom = new QWidget(owner);
        auto *bottomLayout = new QHBoxLayout(bottom);
        bottomLayout->setContentsMargins(0, 4, 0, 0);
        zoomLabel_ = new QLabel(QStringLiteral("100%"), bottom);
        zoomCombo_ = new QComboBox(bottom);
        zoomCombo_->addItems({QStringLiteral("25%"), QStringLiteral("50%"),
                              QStringLiteral("100%"), QStringLiteral("200%"),
                              tr("Fit")});
        zoomCombo_->setCurrentIndex(4);
        connect(zoomCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
                owner_, [this](int i) { canvas_->setZoomIndex(i); });
        bottomLayout->addWidget(zoomLabel_);
        bottomLayout->addWidget(zoomCombo_);
        bottomLayout->addStretch(1);
        previewBox_ = new QCheckBox(tr("Preview"), bottom);
        previewBox_->setChecked(true);
        connect(previewBox_, &QCheckBox::toggled, owner_, [this](bool on) { setPreviewOn(on); });
        bottomLayout->addWidget(previewBox_);
        liveBox_ = new QCheckBox(tr("Live filter"), bottom);
        liveBox_->setToolTip(tr("Keep the pixels and re-apply on every edit instead of baking."));
        bottomLayout->addWidget(liveBox_);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, bottom);
        QObject::connect(buttons, &QDialogButtonBox::accepted, owner, [this] {
            owner_->accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, owner, [this] {
            owner_->reject();
        });
        bottomLayout->addWidget(buttons);
        auto *column = new QVBoxLayout(owner);
        column->setContentsMargins(16, 16, 16, 16);
        column->setSpacing(12);
        column->addWidget(middle, 1);
        column->addWidget(bottom);
        connect(canvas_, &FilterPreviewCanvas::zoomChanged, owner_, [this] { syncZoomWidgets(); });
        previewTimer_ = new QTimer(owner_);
        previewTimer_->setSingleShot(true);
        previewTimer_->setInterval(80);
        connect(previewTimer_, &QTimer::timeout, owner_, [this] { preview(); });
        canvas_->refresh();
        preview();
        lastApply_.start();
        syncZoomWidgets();
    }

    void requestPreview() {
        LayerItem *layer = state_->activeLayer();
        const int gap = (layer && useProxy(layer)) ? 30 : 80;
        if (!previewTimer_ || !lastApply_.isValid() || lastApply_.elapsed() > gap) {
            if (previewTimer_) previewTimer_->setInterval(gap);
            preview();
            lastApply_.start();
            return;
        }
        previewTimer_->start();
    }

    void flushPendingPreview() {
        if (previewTimer_ && previewTimer_->isActive()) {
            previewTimer_->stop();
            preview();
            lastApply_.start();
        }
    }

    void setPreviewOn(bool on) {
        if (on == previewOn_) return;
        previewOn_ = on;
        if (on) {
            preview();
        } else {
            session_.reject();
            canvas_->clearPreviewImage();
            canvas_->refresh();
        }
    }

    void syncZoomWidgets() {
        if (zoomLabel_)
            zoomLabel_->setText(QStringLiteral("%1%").arg(canvas_->zoom() * 100.0, 0, 'f', 1));
        if (zoomCombo_) {
            QSignalBlocker block(zoomCombo_);
            zoomCombo_->setCurrentIndex(canvas_->zoomIndex());
        }
    }

    void addParamRow(QFormLayout *form, const filter::FilterParam &p) {
        Row row;
        row.kind = p.kind;
        row.min = p.min;
        row.max = p.max;
        if (p.kind == 1) {
            auto *combo = new QComboBox;
            for (const char *opt : p.choices) {
                QString label = QString::fromUtf8(opt);
                if (!label.isEmpty()) label[0] = label[0].toUpper();
                combo->addItem(label);
            }
            combo->setCurrentIndex(qBound(0, static_cast<int>(std::round(p.def)), combo->count() - 1));
            QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                             owner_, [this] { requestPreview(); });
            row.combo = combo;
            rows_.push_back(row);
            form->addRow(QString::fromUtf8(p.label), combo);
        } else if (p.kind == 2) {
            auto *check = new QCheckBox(QString::fromUtf8(p.label));
            check->setChecked(p.def > 0.5);
            QObject::connect(check, &QCheckBox::toggled, owner_, [this] { requestPreview(); });
            row.check = check;
            rows_.push_back(row);
            form->addRow(check);
        } else {
            auto *slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 1000);
            auto *spin = new QDoubleSpinBox;
            spin->setRange(p.min, p.max);
            const double span = p.max - p.min;
            spin->setDecimals(span > 200 ? 0 : (span > 20 ? 1 : 2));
            spin->setSingleStep(span > 200 ? 1.0 : (span > 2 ? 0.5 : 0.05));
            if (p.suffix && p.suffix[0]) spin->setSuffix(QString::fromUtf8(p.suffix));
            spin->setValue(p.def);
            slider->setValue(qBound(0, static_cast<int>(std::round((p.def - p.min) / span * 1000.0)), 1000));
            QObject::connect(slider, &QSlider::valueChanged, owner_, [this, slider, spin, p] {
                QSignalBlocker block(spin);
                spin->setValue(p.min + (p.max - p.min) * slider->value() / 1000.0);
                requestPreview();
            });
            QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                             owner_, [this, slider, spin, p] {
                QSignalBlocker block(slider);
                const double span2 = p.max - p.min;
                slider->setValue(span2 > 0 ? qBound(0, static_cast<int>(std::round((spin->value() - p.min) / span2 * 1000.0)), 1000) : 0);
                requestPreview();
            });
            row.slider = slider;
            row.spin = spin;
            rows_.push_back(row);
            auto *cell = new QHBoxLayout;
            cell->addWidget(slider, 1);
            cell->addWidget(spin);
            auto *label = new QLabel(QString::fromUtf8(p.label));
            form->addRow(label, cell);
        }
    }

    static QVector<QPair<QString, QVector<int>>> sectionsFor(const QString &filterId) {
        static const QMap<QString, QVector<QPair<QString, QVector<int>>>> table = {
            {QStringLiteral("camera_raw"), {
                {QObject::tr("Tone"), {2, 3, 4, 5, 6, 7, 8, 9}},
                {QObject::tr("Color"), {0, 1, 10, 11}},
                {QObject::tr("Detail"), {12, 13}},
                {QObject::tr("Effects"), {14}},
            }},
            {QStringLiteral("lens_correction"), {
                {QObject::tr("Geometry"), {0, 5, 6, 7, 8}},
                {QObject::tr("Chromatic"), {1, 2}},
                {QObject::tr("Vignette"), {3, 4}},
            }},
            {QStringLiteral("lighting_effects"), {
                {QObject::tr("Light"), {0, 1, 2, 3, 4, 5}},
                {QObject::tr("Properties"), {6, 7, 8}},
            }},
            {QStringLiteral("flame"), {
                {QObject::tr("Shape"), {0, 1, 2, 3}},
                {QObject::tr("Texture"), {4, 5, 6}},
            }},
            {QStringLiteral("tree"), {
                {QObject::tr("Trunk"), {0, 1}},
                {QObject::tr("Canopy"), {2, 3, 4, 5, 6}},
            }},
            {QStringLiteral("iris_blur"), {
                {QObject::tr("Blur"), {0, 3, 4, 5, 6}},
                {QObject::tr("Center"), {1, 2}},
            }},
            {QStringLiteral("tilt_shift"), {
                {QObject::tr("Blur"), {0, 2, 3, 4}},
                {QObject::tr("Focus"), {1}},
            }},
            {QStringLiteral("spin_blur"), {
                {QObject::tr("Blur"), {0, 3, 4}},
                {QObject::tr("Center"), {1, 2}},
            }},
            {QStringLiteral("radial_blur"), {
                {QObject::tr("Blur"), {0, 1, 2}},
                {QObject::tr("Center"), {3, 4}},
            }},
            {QStringLiteral("wave"), {
                {QObject::tr("Wave"), {0, 1, 2, 5, 6}},
                {QObject::tr("Scale"), {3, 4}},
            }},
            {QStringLiteral("color_halftone"), {
                {QObject::tr("Screen"), {0}},
                {QObject::tr("Angles"), {1, 2, 3, 4}},
            }},
            {QStringLiteral("conte_crayon"), {
                {QObject::tr("Levels"), {0, 1}},
                {QObject::tr("Texture"), {2, 3, 4, 5, 6}},
            }},
        };
        return table.value(filterId);
    }

    std::vector<double> collectValues() {
        std::vector<double> v;
        for (const auto &r : rows_) {
            if (r.kind == 1 && r.combo) v.push_back(r.combo->currentIndex());
            else if (r.kind == 2 && r.check) v.push_back(r.check->isChecked() ? 1.0 : 0.0);
            else if (r.spin) v.push_back(r.spin->value());
            else v.push_back(0.0);
        }
        return v;
    }

    bool useProxy(LayerItem *layer) const {
        if (!layer || !layer->pixels) return false;
        const std::size_t n = static_cast<std::size_t>(layer->pixels->width()) * layer->pixels->height();
        return n > 1000000;
    }

    std::shared_ptr<pittore::Image> downscaled(const pittore::Image &s) {
        const std::uint32_t w = s.width();
        const std::uint32_t h = s.height();
        const std::uint32_t m = w > h ? w : h;
        std::uint32_t f = (m + 959) / 960;
        if (f < 1) f = 1;
        const std::uint32_t pw = (w + f - 1) / f;
        const std::uint32_t ph = (h + f - 1) / f;
        auto o = std::make_shared<pittore::Image>(pw, ph);
        for (std::uint32_t y = 0; y < ph; ++y) {
            for (std::uint32_t x = 0; x < pw; ++x) {
                double r = 0, g = 0, b = 0, a = 0;
                std::uint32_t cnt = 0;
                for (std::uint32_t dy = 0; dy < f; ++dy) {
                    const std::uint32_t sy = y * f + dy;
                    if (sy >= h) break;
                    for (std::uint32_t dx = 0; dx < f; ++dx) {
                        const std::uint32_t sx = x * f + dx;
                        if (sx >= w) break;
                        const pittore::RGBAf &p = s.at(sx, sy);
                        r += p.r;
                        g += p.g;
                        b += p.b;
                        a += p.a;
                        ++cnt;
                    }
                }
                const float k = cnt ? 1.0f / static_cast<float>(cnt) : 1.0f;
                pittore::RGBAf &t = o->at(x, y);
                t.r = static_cast<float>(r) * k;
                t.g = static_cast<float>(g) * k;
                t.b = static_cast<float>(b) * k;
                t.a = static_cast<float>(a) * k;
            }
        }
        return o;
    }

    QImage imageToQ(const pittore::Image &img) {
        QImage out(int(img.width()), int(img.height()), QImage::Format_ARGB32_Premultiplied);
        for (std::uint32_t y = 0; y < img.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(out.scanLine(int(y)));
            for (std::uint32_t x = 0; x < img.width(); ++x) {
                const pittore::RGBAf &p = img.at(x, y);
                const float a = std::clamp(p.a, 0.0f, 1.0f);
                row[x] = qRgba(int(std::clamp(p.r, 0.0f, 1.0f) * a * 255.0f),
                               int(std::clamp(p.g, 0.0f, 1.0f) * a * 255.0f),
                               int(std::clamp(p.b, 0.0f, 1.0f) * a * 255.0f),
                               int(a * 255.0f));
            }
        }
        return out;
    }

    void ensurePristine(LayerItem *layer) {
        if (!pristineSrc_ || pristineSrc_->width() != layer->pixels->width() ||
            pristineSrc_->height() != layer->pixels->height()) {
            pristineSrc_ = std::make_shared<pittore::Image>(layer->pixels->clone());
        }
    }

    void previewProxy(LayerItem *layer) {
        ensurePristine(layer);
        QElapsedTimer perf;
        perf.start();
        auto small = downscaled(*pristineSrc_);
        pittore::filter::applyFilter(*small, id_.toStdString(), values_);
        const double applyMs = perf.nsecsElapsed() / 1e6;
        perf.restart();
        canvas_->setPreviewImage(imageToQ(*small));
        const double canvasMs = perf.nsecsElapsed() / 1e6;
        PITTORE_LOG("[filter-preview] id=%s apply=%.1fms composite=%.1fms canvas=%.1fms",
                     id_.toStdString().c_str(), applyMs, 0.0, canvasMs);
    }

    void preview() {
        if (!session_.ensure()) {
            state_->setStatusHint(QObject::tr("Open a document with an editable pixel layer first."));
            return;
        }
        LayerItem *layer = state_->activeLayer();
        if (!layer || !layer->pixels) return;
        values_ = collectValues();
        if (!previewOn_) {
            canvas_->clearPreviewImage();
            canvas_->refresh();
            return;
        }
        if (useProxy(layer)) {
            previewProxy(layer);
            return;
        }
        canvas_->clearPreviewImage();
        ensurePristine(layer);
        if (layer->pixels->width() == pristineSrc_->width() &&
            layer->pixels->height() == pristineSrc_->height()) {
            *layer->pixels = *pristineSrc_;
        }
        QElapsedTimer perf;
        perf.start();
        state_->applyFilterToActiveLayer(id_.toStdString(), values_);
        const double applyMs = perf.nsecsElapsed() / 1e6;
        perf.restart();
        session_.preview();
        const double compositeMs = perf.nsecsElapsed() / 1e6;
        perf.restart();
        canvas_->refresh();
        const double canvasMs = perf.nsecsElapsed() / 1e6;
        PITTORE_LOG("[filter-preview] id=%s apply=%.1fms composite=%.1fms canvas=%.1fms",
                     id_.toStdString().c_str(), applyMs, compositeMs, canvasMs);
    }

    void accept() {
        flushPendingPreview();
        // Live branch: throw away the destructive preview session (pixels
        // restored to pre-dialog state) and store the recipe instead. The
        // native pixels are never mutated.
        if (liveBox_ && liveBox_->isChecked()) {
            values_ = collectValues();
            session_.reject();
            if (canvas_) canvas_->clearPreviewImage();
            state_->convertToLiveFilter(id_, values_);
            return;
        }
        const bool wasOff = !previewOn_;
        if (wasOff) {
            previewOn_ = true;
            if (previewBox_) {
                QSignalBlocker block(previewBox_);
                previewBox_->setChecked(true);
            }
        }
        LayerItem *layer = state_->activeLayer();
        if (layer && layer->pixels && useProxy(layer)) {
            values_ = collectValues();
            ensurePristine(layer);
            if (layer->pixels->width() == pristineSrc_->width() &&
                layer->pixels->height() == pristineSrc_->height()) {
                *layer->pixels = *pristineSrc_;
            }
            state_->applyFilterToActiveLayer(id_.toStdString(), values_);
            session_.preview();
            canvas_->clearPreviewImage();
        } else if (wasOff) {
            preview();
        }
        session_.accept();
    }
    void reject() {
        pristineSrc_.reset();
        if (canvas_) canvas_->clearPreviewImage();
        session_.reject();
    }

    FilterDialog *owner_;
    AppState *state_;
    QString id_;
    FilterSession session_;
    std::vector<Row> rows_;
    std::vector<double> values_;
    FilterPreviewCanvas *canvas_ = nullptr;
    QTimer *previewTimer_ = nullptr;
    QElapsedTimer lastApply_;
    QCheckBox *previewBox_ = nullptr;
    QCheckBox *liveBox_ = nullptr;
    QComboBox *zoomCombo_ = nullptr;
    QLabel *zoomLabel_ = nullptr;
    bool previewOn_ = true;
    std::shared_ptr<pittore::Image> pristineSrc_;
};

FilterDialog::FilterDialog(AppState *state, const QString &filterId, QWidget *parent)
    : QDialog(parent), impl_(new Impl(this, state, filterId)) {
    const filter::FilterDef *def = filter::findFilter(filterId.toStdString());
    setWindowTitle(def ? QString::fromUtf8(def->name) : filterId);
    setModal(true);
    resize(1060, 660);
}

QString FilterDialog::filterId() const { return impl_->id_; }

void FilterDialog::accept() {
    impl_->accept();
    QDialog::accept();
}

void FilterDialog::reject() {
    impl_->reject();
    QDialog::reject();
}

std::vector<double> FilterDialog::appliedParams() const { return impl_->values_; }

bool FilterDialog::showingProxyPreview() const {
    return impl_->canvas_ && impl_->canvas_->hasPreviewImage();
}

void FilterDialog::preview() { impl_->preview(); }

QVector<QPair<QString, QString>> galleryFilterEntries(const QString &category) {
    QVector<QPair<QString, QString>> out;
    for (const auto &d : filter::allFilterDefs()) {
        const QString cat = QString::fromUtf8(d.category);
        if (!category.isEmpty() && cat.compare(category, Qt::CaseInsensitive) != 0) {
            if (!(category.compare(QStringLiteral("Gallery"), Qt::CaseInsensitive) == 0 &&
                  (cat == QStringLiteral("Artistic") || cat == QStringLiteral("Sketch") ||
                   cat == QStringLiteral("Brush Strokes") || cat == QStringLiteral("Texture") ||
                   cat == QStringLiteral("Stylize"))))
                continue;
        }
        out.push_back({QString::fromUtf8(d.name), QString::fromUtf8(d.id)});
    }
    return out;
}

class FilterGalleryDialog::Impl {
public:
    Impl(FilterGalleryDialog *owner, AppState *state, const QString &category)
        : owner_(owner), state_(state) {
        auto *top = new QHBoxLayout;
        search_ = new QLineEdit;
        search_->setPlaceholderText(QObject::tr("Search filters…"));
        search_->setClearButtonEnabled(true);
        top->addWidget(search_, 1);
        cats_ = new QComboBox;
        cats_->addItem(QObject::tr("All"));
        cats_->addItem(QStringLiteral("Gallery"));
        QStringList seen;
        for (const auto &d : filter::allFilterDefs()) {
            const QString c = QString::fromUtf8(d.category);
            if (!seen.contains(c)) {
                seen.push_back(c);
                cats_->addItem(c);
            }
        }
        if (!category.isEmpty()) {
            const int idx = cats_->findText(category);
            if (idx >= 0) cats_->setCurrentIndex(idx);
        }
        top->addWidget(cats_);
        list_ = new QListWidget;
        list_->setUniformItemSizes(true);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        QObject::connect(buttons, &QDialogButtonBox::rejected, owner, &QDialog::reject);
        apply_ = new QPushButton(QObject::tr("Apply…"));
        buttons->addButton(apply_, QDialogButtonBox::AcceptRole);
        auto *layout = new QVBoxLayout(owner);
        layout->addLayout(top);
        layout->addWidget(list_, 1);
        layout->addWidget(buttons);
        QObject::connect(search_, &QLineEdit::textChanged, owner, [this] { rebuild(); });
        QObject::connect(cats_, &QComboBox::currentIndexChanged, owner, [this] { rebuild(); });
        QObject::connect(list_, &QListWidget::itemActivated, owner, [this] { openSelected(); });
        QObject::connect(apply_, &QPushButton::clicked, owner, [this] { openSelected(); });
        rebuild();
    }

    void rebuild() {
        list_->clear();
        const QString q = search_->text().trimmed().toLower();
        QString cat = cats_->currentText();
        if (cat == QObject::tr("All")) cat.clear();
        for (const auto &e : galleryFilterEntries(cat)) {
            if (!q.isEmpty() && !e.first.toLower().contains(q)) continue;
            auto *item = new QListWidgetItem(e.first, list_);
            item->setData(Qt::UserRole, e.second);
        }
        if (list_->count() > 0) list_->setCurrentRow(0);
    }

    void openSelected() {
        auto *item = list_->currentItem();
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        FilterDialog dlg(state_, id, owner_);
        if (dlg.exec() == QDialog::Accepted) {
            if (onApplied) onApplied(id, dlg.appliedParams());
        }
    }

    FilterGalleryDialog *owner_;
    AppState *state_;
    QLineEdit *search_ = nullptr;
    QComboBox *cats_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *apply_ = nullptr;
    std::function<void(const QString &, const std::vector<double> &)> onApplied;
};

FilterGalleryDialog::FilterGalleryDialog(AppState *state, const QString &category, QWidget *parent)
    : QDialog(parent), impl_(new Impl(this, state, category)) {
    setWindowTitle(tr("Filter Gallery"));
    resize(520, 560);
    setModal(true);
}

void FilterGalleryDialog::setCategory(const QString &category) {
    const int idx = impl_->cats_->findText(category);
    if (idx >= 0) impl_->cats_->setCurrentIndex(idx);
    impl_->rebuild();
}

}
