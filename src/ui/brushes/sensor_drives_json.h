#pragma once
// QString/QJson boundary for sensor drives: preset JSON arrays and the
// per-tool `sensor_drives` option string. The math core in sensor_drives.h
// stays Qt-free and engine-testable; everything here is a thin conversion.
#include <QString>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <string>
#include <vector>

#include "ui/brushes/pressure_curve.h"
#include "ui/brushes/sensor_drives.h"

namespace pittore::ui::sensordrive {

inline QJsonObject driveToJson(const SensorDrive& d) {
    QJsonObject o{{"prop", QString::fromStdString(d.prop)},
                   {"sensor", QString::fromUtf8(sensorName(d.sensor))},
                   {"amount", d.amount}};
    if (!d.curve.empty())
        o.insert("curve", QString::fromStdString(d.curve));
    if (d.lengthPx != 500.0) o.insert("length", d.lengthPx);
    if (d.timeSec != 5.0) o.insert("time", d.timeSec);
    return o;
}

inline SensorDrive driveFromJson(const QJsonObject& o, bool* ok = nullptr) {
    SensorDrive d;
    d.prop = o.value(QStringLiteral("prop")).toString().toStdString();
    d.sensor =
        sensorFromName(o.value(QStringLiteral("sensor")).toString().toStdString());
    d.amount = std::clamp(o.value(QStringLiteral("amount")).toDouble(0.0),
                           -100.0, 100.0);
    d.curve = o.value(QStringLiteral("curve")).toString().toStdString();
    d.lengthPx = std::clamp(o.value(QStringLiteral("length")).toDouble(500.0),
                             1.0, 20000.0);
    d.timeSec = std::clamp(o.value(QStringLiteral("time")).toDouble(5.0), 0.1,
                            3600.0);
    const bool valid = !d.prop.empty() && d.amount != 0.0;
    if (ok) *ok = valid;
    return d;
}

inline QString encodeDrives(const std::vector<SensorDrive>& drives) {
    QJsonArray arr;
    for (const auto& d : drives) {
        if (d.prop.empty() || d.amount == 0.0) continue;
        arr.push_back(driveToJson(d));
    }
    if (arr.isEmpty()) return QString();
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

inline std::vector<SensorDrive> decodeDrives(const QString& s) {
    std::vector<SensorDrive> out;
    if (s.trimmed().isEmpty()) return out;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return out;
    for (const QJsonValue& v : doc.array()) {
        if (!v.isObject()) continue;
        bool ok = false;
        SensorDrive d = driveFromJson(v.toObject(), &ok);
        if (ok) out.push_back(d);
    }
    return out;
}

// Production curve shaper: the shared authored-curve evaluator.
inline double brushCurveShape(const std::string& curve, double raw) {
    const auto c =
        brushcurve::parseEncoded(QString::fromStdString(curve));
    if (!c.has()) return raw;
    return std::clamp(brushcurve::eval(c, raw), 0.0, 1.0);
}

}  // namespace pittore::ui::sensordrive
