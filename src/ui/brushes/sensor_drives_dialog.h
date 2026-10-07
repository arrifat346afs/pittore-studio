#pragma once
// Sensor drives editor: which sensor scales which dab property, by how
// much, and with what response curve. Live-applies onto the tool's
// `sensor_drives` option; presets snapshot it like any other option.
// Only properties with engine support are offered (scatter, size,
// opacity, flow) — the list grows as more properties get drive clients.
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

#include "ui/app_state.h"
#include "ui/brushes/sensor_drives_json.h"
#include "ui/curve_editor.h"

namespace pittore::ui::sensordrive {

inline QStringList drivePropIds() {
    return {QStringLiteral("scatter"), QStringLiteral("size"),
            QStringLiteral("opacity"), QStringLiteral("flow")};
}

inline QString drivePropLabel(const QString& id) {
    if (id == QLatin1String("size")) return QObject::tr("Size");
    if (id == QLatin1String("opacity")) return QObject::tr("Opacity");
    if (id == QLatin1String("flow")) return QObject::tr("Flow");
    return QObject::tr("Scatter");
}

inline QStringList driveSensorIds() {
    return {QStringLiteral("pressure"),   QStringLiteral("hold"),
            QStringLiteral("tiltx"),      QStringLiteral("tilty"),
            QStringLiteral("lean"),       QStringLiteral("speed"),
            QStringLiteral("distance"),   QStringLiteral("time"),
            QStringLiteral("fuzzydab"),   QStringLiteral("fuzzystroke"),
            QStringLiteral("barrel"),     QStringLiteral("tangential")};
}

inline QString driveSensorLabel(const QString& id) {
    if (id == QLatin1String("hold")) return QObject::tr("Pressure hold");
    if (id == QLatin1String("tiltx")) return QObject::tr("Tilt X");
    if (id == QLatin1String("tilty")) return QObject::tr("Tilt Y");
    if (id == QLatin1String("lean")) return QObject::tr("Tilt lean");
    if (id == QLatin1String("speed")) return QObject::tr("Speed");
    if (id == QLatin1String("distance")) return QObject::tr("Distance");
    if (id == QLatin1String("time")) return QObject::tr("Time");
    if (id == QLatin1String("fuzzydab")) return QObject::tr("Fuzzy dab");
    if (id == QLatin1String("fuzzystroke"))
        return QObject::tr("Fuzzy stroke");
    if (id == QLatin1String("barrel")) return QObject::tr("Barrel");
    if (id == QLatin1String("tangential"))
        return QObject::tr("Tangential");
    return QObject::tr("Pressure");
}

inline QString driveRowLabel(const SensorDrive& d) {
    const int pct = int(std::round(d.amount));
    return QStringLiteral("%1 <- %2  %3%")
        .arg(drivePropLabel(QString::fromStdString(d.prop)),
             driveSensorLabel(
                 QString::fromUtf8(sensorName(d.sensor))),
             QString::number(pct));
}

// Encodes CurveEditor points back into the authored curve string,
// preserving the value header a bundle may have carried.
inline QString encodeDriveCurve(double value, const QVector<QPointF>& pts) {
    QStringList parts;
    for (const QPointF& p : pts)
        parts.push_back(QString::number(p.x()) + QLatin1Char(',') +
                        QString::number(p.y()));
    return QString::number(value) + QLatin1Char('|') + parts.join(';');
}

inline QVector<QPointF> decodeDriveCurvePoints(const QString& curve) {
    const auto c = brushcurve::parseEncoded(curve);
    QVector<QPointF> pts;
    for (const auto& p : c.points)
        pts.push_back(QPointF(p.first, std::clamp(p.second, 0.0, 1.0)));
    if (pts.isEmpty()) pts = {{0.0, 0.0}, {1.0, 1.0}};
    return pts;
}

class SensorDrivesDialog final : public QDialog {
  public:
    std::function<void()> onChanged_;

    SensorDrivesDialog(AppState* state, ToolId tool, QWidget* parent = nullptr)
        : QDialog(parent), state_(state), tool_(tool) {
        setWindowTitle(tr("Sensor drives"));
        auto* outer = new QVBoxLayout(this);
        auto* rows = new QHBoxLayout();
        rows->setContentsMargins(0, 0, 0, 0);
        list_ = new QListWidget(this);
        rows->addWidget(list_, 1);
        auto* side = new QVBoxLayout();
        side->setContentsMargins(0, 0, 0, 0);
        addButton_ = new QPushButton(tr("Add"), this);
        removeButton_ = new QPushButton(tr("Remove"), this);
        side->addWidget(addButton_);
        side->addWidget(removeButton_);
        side->addStretch(1);
        rows->addLayout(side);
        outer->addLayout(rows);

        prop_ = new QComboBox(this);
        for (const QString& id : drivePropIds())
            prop_->addItem(drivePropLabel(id), id);
        sensor_ = new QComboBox(this);
        for (const QString& id : driveSensorIds())
            sensor_->addItem(driveSensorLabel(id), id);
        auto* form = new QVBoxLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->addWidget(new QLabel(tr("Property"), this));
        form->addWidget(prop_);
        form->addWidget(new QLabel(tr("Sensor"), this));
        form->addWidget(sensor_);
        auto* amtRow = new QHBoxLayout();
        amtRow->setContentsMargins(0, 0, 0, 0);
        amount_ = new QSlider(Qt::Horizontal, this);
        amount_->setRange(-100, 100);
        amountSpin_ = new QSpinBox(this);
        amountSpin_->setRange(-100, 100);
        amountSpin_->setSuffix(tr("%"));
        amtRow->addWidget(new QLabel(tr("Amount"), this));
        amtRow->addWidget(amount_, 1);
        amtRow->addWidget(amountSpin_);
        form->addLayout(amtRow);
        auto* spanRow = new QHBoxLayout();
        spanRow->setContentsMargins(0, 0, 0, 0);
        length_ = new QDoubleSpinBox(this);
        length_->setRange(1.0, 20000.0);
        length_->setSuffix(tr(" px"));
        time_ = new QDoubleSpinBox(this);
        time_->setRange(0.1, 3600.0);
        time_->setSuffix(tr(" s"));
        spanRow->addWidget(new QLabel(tr("Distance span"), this));
        spanRow->addWidget(length_, 1);
        spanRow->addWidget(new QLabel(tr("Time span"), this));
        spanRow->addWidget(time_, 1);
        form->addLayout(spanRow);
        outer->addLayout(form);

        curve_ = new CurveEditor(this);
        curve_->setMinimumHeight(160);
        outer->addWidget(new QLabel(tr("Response"), this));
        outer->addWidget(curve_);
        auto* curveRow = new QHBoxLayout();
        curveRow->setContentsMargins(0, 0, 0, 0);
        auto* resetCurve = new QPushButton(tr("Linear response"), this);
        curveRow->addWidget(resetCurve);
        curveRow->addStretch(1);
        outer->addLayout(curveRow);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        outer->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(addButton_, &QPushButton::clicked, this,
                [this] { addDrive(); });
        connect(removeButton_, &QPushButton::clicked, this,
                [this] { removeDrive(); });
        connect(list_, &QListWidget::currentRowChanged, this,
                [this](int) { refreshEditors(); });
        connect(prop_, &QComboBox::currentIndexChanged, this,
                [this](int) { commitRow(); });
        connect(sensor_, &QComboBox::currentIndexChanged, this,
                [this](int) { commitRow(); });
        connect(amount_, &QSlider::valueChanged, this, [this](int v) {
            amountSpin_->blockSignals(true);
            amountSpin_->setValue(v);
            amountSpin_->blockSignals(false);
            commitRow();
        });
        connect(amountSpin_, &QSpinBox::valueChanged, this, [this](int v) {
            amount_->blockSignals(true);
            amount_->setValue(v);
            amount_->blockSignals(false);
            commitRow();
        });
        connect(length_, &QDoubleSpinBox::valueChanged, this,
                [this](double) { commitRow(); });
        connect(time_, &QDoubleSpinBox::valueChanged, this,
                [this](double) { commitRow(); });
        curve_->onChanged_ = [this] { commitCurve(); };
        connect(resetCurve, &QPushButton::clicked, this, [this] {
            const int row = list_->currentRow();
            if (row < 0 || row >= (int)drives_.size()) return;
            drives_.at(row).curve.clear();
            save();
            refreshEditors();
        });
        reload();
    }

  private:
    void reload() {
        drives_ = decodeDrives(
            state_->option(tool_, QStringLiteral("sensor_drives")).toString());
        list_->blockSignals(true);
        list_->clear();
        for (const auto& d : drives_)
            list_->addItem(driveRowLabel(d));
        list_->blockSignals(false);
        if (!drives_.empty() && list_->currentRow() < 0)
            list_->setCurrentRow(0);
        refreshEditors();
    }

    void refreshEditors() {
        const int row = list_->currentRow();
        const bool has =
            row >= 0 && row < (int)drives_.size() && !drives_.empty();
        prop_->setEnabled(has);
        sensor_->setEnabled(has);
        amount_->setEnabled(has);
        amountSpin_->setEnabled(has);
        length_->setEnabled(has);
        time_->setEnabled(has);
        curve_->setEnabled(has);
        removeButton_->setEnabled(has);
        if (!has) return;
        const SensorDrive& d = drives_.at(row);
        const QString propId = QString::fromStdString(d.prop);
        prop_->blockSignals(true);
        const int pi = prop_->findData(propId);
        prop_->setCurrentIndex(pi >= 0 ? pi : 0);
        prop_->blockSignals(false);
        sensor_->blockSignals(true);
        const int si = sensor_->findData(
            QString::fromUtf8(sensorName(d.sensor)));
        sensor_->setCurrentIndex(si >= 0 ? si : 0);
        sensor_->blockSignals(false);
        amount_->blockSignals(true);
        amountSpin_->blockSignals(true);
        amount_->setValue(int(std::round(d.amount)));
        amountSpin_->setValue(int(std::round(d.amount)));
        amount_->blockSignals(false);
        amountSpin_->blockSignals(false);
        length_->blockSignals(true);
        time_->blockSignals(true);
        length_->setValue(d.lengthPx);
        time_->setValue(d.timeSec);
        length_->blockSignals(false);
        time_->blockSignals(false);
        const bool dist = d.sensor == Sensor::Distance;
        const bool tim = d.sensor == Sensor::Time;
        length_->setEnabled(has && dist);
        time_->setEnabled(has && tim);
        curve_->blockSignals(true);
        curve_->setPoints(decodeDriveCurvePoints(
            QString::fromStdString(d.curve)));
        curve_->blockSignals(false);
    }

    void save() {
        // Zero-amount drives are off: drop them instead of persisting
        // dead rows.
        std::vector<SensorDrive> kept;
        for (const auto& d : drives_) {
            if (d.amount == 0.0) continue;
            kept.push_back(d);
        }
        drives_.swap(kept);
        state_->setOption(tool_, QStringLiteral("sensor_drives"),
                          encodeDrives(drives_));
        if (onChanged_) onChanged_();
    }

    void refreshRowLabel(int row) {
        if (row < 0 || row >= (int)drives_.size()) return;
        if (QListWidgetItem* item = list_->item(row))
            item->setText(driveRowLabel(drives_.at(row)));
    }

    void commitRow() {
        const int row = list_->currentRow();
        if (row < 0 || row >= (int)drives_.size()) return;
        SensorDrive& d = drives_.at(row);
        d.prop = prop_->currentData().toString().toStdString();
        d.sensor = sensorFromName(
            sensor_->currentData().toString().toStdString());
        d.amount = std::clamp(double(amount_->value()), -100.0, 100.0);
        d.lengthPx = std::clamp(length_->value(), 1.0, 20000.0);
        d.timeSec = std::clamp(time_->value(), 0.1, 3600.0);
        if (d.amount == 0.0) {
            drives_.erase(drives_.begin() + row);
            save();
            reload();
            return;
        }
        save();
        refreshRowLabel(row);
        refreshEditors();
    }

    void commitCurve() {
        const int row = list_->currentRow();
        if (row < 0 || row >= (int)drives_.size()) return;
        const auto c = brushcurve::parseEncoded(
            QString::fromStdString(drives_.at(row).curve));
        QStringList parts;
        for (const QPointF& p : curve_->points())
            parts.push_back(QString::number(p.x()) + QLatin1Char(',') +
                            QString::number(p.y()));
        drives_.at(row).curve =
            (QString::number(c.value) + QLatin1Char('|') + parts.join(';'))
                .toStdString();
        save();
    }

    void addDrive() {
        SensorDrive d;
        d.prop = "scatter";
        d.sensor = Sensor::Pressure;
        d.amount = 50.0;
        drives_.push_back(d);
        save();
        reload();
        list_->setCurrentRow(int(drives_.size()) - 1);
    }

    void removeDrive() {
        const int row = list_->currentRow();
        if (row < 0 || row >= (int)drives_.size()) return;
        drives_.erase(drives_.begin() + row);
        save();
        reload();
    }

    AppState* state_;
    ToolId tool_;
    std::vector<SensorDrive> drives_;
    QListWidget* list_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QComboBox* prop_ = nullptr;
    QComboBox* sensor_ = nullptr;
    QSlider* amount_ = nullptr;
    QSpinBox* amountSpin_ = nullptr;
    QDoubleSpinBox* length_ = nullptr;
    QDoubleSpinBox* time_ = nullptr;
    CurveEditor* curve_ = nullptr;
};

}  // namespace pittore::ui::sensordrive
