#pragma once
// Generic sensor drives: any tablet/stroke sensor can scale any dab property.
//
// A drive adds `amount/100 * shaped(sensor)` to a property factor that starts
// at exactly 1.0, so presets without drives paint bit-identically to legacy.
// `curve` reuses the authored pressure-curve encoding ("value|x,y;...");
// empty means a linear response. The property id is a plain string
// ("scatter", later "size", "opacity", ...) so old presets and future
// properties stay forward compatible.
//
// Qt-free on purpose (plain std::string): the math stays unit-testable in
// the engine test suite. QString/QJson conversion lives in
// sensor_drives_json.h at the UI boundary. Original design, no borrowed code.
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace pittore::ui::sensordrive {

enum class Sensor : int {
    Pressure = 0,  // live stylus pressure 0..1 (mouse: 1)
    Hold,          // stroke running-maximum pressure (monotonic inking)
    TiltX,         // |x lean| / 60deg
    TiltY,         // |y lean| / 60deg
    Lean,          // combined lean magnitude / 60deg
    Speed,         // stroke speed, caller-normalized 0..1
    Distance,      // travelled px over the drive's own lengthPx
    Time,          // elapsed sec over the drive's own timeSec
    FuzzyDab,      // per-dab random (drawn only when a drive needs it)
    FuzzyStroke,   // one hash per stroke, stable across the stroke
    Barrel,        // stylus rotation 0..1 around the dial
    Tangential,    // wheel pressure 0..1 (0 without a tablet stroke)
};

inline const char* sensorName(Sensor s) {
    switch (s) {
        case Sensor::Pressure: return "pressure";
        case Sensor::Hold: return "hold";
        case Sensor::TiltX: return "tiltx";
        case Sensor::TiltY: return "tilty";
        case Sensor::Lean: return "lean";
        case Sensor::Speed: return "speed";
        case Sensor::Distance: return "distance";
        case Sensor::Time: return "time";
        case Sensor::FuzzyDab: return "fuzzydab";
        case Sensor::FuzzyStroke: return "fuzzystroke";
        case Sensor::Barrel: return "barrel";
        case Sensor::Tangential: return "tangential";
    }
    return "pressure";
}

inline Sensor sensorFromName(const std::string& n) {
    if (n == "hold") return Sensor::Hold;
    if (n == "tiltx") return Sensor::TiltX;
    if (n == "tilty") return Sensor::TiltY;
    if (n == "lean") return Sensor::Lean;
    if (n == "speed") return Sensor::Speed;
    if (n == "distance") return Sensor::Distance;
    if (n == "time") return Sensor::Time;
    if (n == "fuzzydab") return Sensor::FuzzyDab;
    if (n == "fuzzystroke") return Sensor::FuzzyStroke;
    if (n == "barrel") return Sensor::Barrel;
    if (n == "tangential") return Sensor::Tangential;
    return Sensor::Pressure;
}

struct SensorDrive {
    std::string prop;  // dab property id, e.g. "scatter"
    Sensor sensor = Sensor::Pressure;
    double amount = 0.0;  // -100..100, 0 = off
    std::string curve;    // authored response, empty = linear
    double lengthPx = 500.0;  // distance-sensor span in travelled px
    double timeSec = 5.0;     // time-sensor span in seconds
};

// Live sensor snapshot for one dab. The caller fills it from the stylus
// latches and stroke clocks; fuzzyDab is drawn from the stroke RNG only
// when a drive actually needs it, so legacy streams never shift.
struct SensorState {
    double pressure = 1.0;
    double hold = 1.0;
    double tiltXDeg = 0.0;
    double tiltYDeg = 0.0;
    double speed01 = 0.0;
    double distPx = 0.0;
    double timeSec = 0.0;
    double fuzzyDab01 = 0.5;
    double fuzzyStroke01 = 0.5;
    double barrel01 = 0.0;
    double tangential01 = 0.0;
    bool tabletDown = false;
};

inline double sensorValue01(Sensor s, const SensorState& st,
                            const SensorDrive& d) {
    switch (s) {
        case Sensor::Pressure:
            return std::clamp(st.pressure, 0.0, 1.0);
        case Sensor::Hold:
            return std::clamp(st.hold, 0.0, 1.0);
        case Sensor::TiltX:
            return std::clamp(std::fabs(st.tiltXDeg) / 60.0, 0.0, 1.0);
        case Sensor::TiltY:
            return std::clamp(std::fabs(st.tiltYDeg) / 60.0, 0.0, 1.0);
        case Sensor::Lean:
            return std::clamp(
                std::hypot(st.tiltXDeg, st.tiltYDeg) / 60.0, 0.0, 1.0);
        case Sensor::Speed:
            return std::clamp(st.speed01, 0.0, 1.0);
        case Sensor::Distance:
            return std::clamp(st.distPx / std::max(d.lengthPx, 1.0), 0.0,
                              1.0);
        case Sensor::Time:
            return std::clamp(st.timeSec / std::max(d.timeSec, 0.1), 0.0,
                              1.0);
        case Sensor::FuzzyDab:
            return std::clamp(st.fuzzyDab01, 0.0, 1.0);
        case Sensor::FuzzyStroke:
            return std::clamp(st.fuzzyStroke01, 0.0, 1.0);
        case Sensor::Barrel:
            return std::clamp(st.barrel01, 0.0, 1.0);
        case Sensor::Tangential:
            return st.tabletDown ? std::clamp(st.tangential01, 0.0, 1.0)
                                 : 0.0;
    }
    return 1.0;
}

// Curve shaper: maps (curve encoding, raw 0..1) to shaped 0..1. Production
// passes the shared pressure-curve evaluator; tests pass scripted lambdas.
using CurveShaper = std::function<double(const std::string&, double)>;

// Multiplicative factor for one property: 1 + every matching drive's
// shaped contribution. Exactly 1.0 when nothing drives the property.
inline double driveFactor(const std::vector<SensorDrive>& drives,
                          const std::string& prop, const SensorState& st,
                          const CurveShaper& shape) {
    double f = 1.0;
    for (const auto& d : drives) {
        if (d.prop != prop || d.amount == 0.0) continue;
        const double raw = sensorValue01(d.sensor, st, d);
        const double shaped =
            d.curve.empty() ? raw : std::clamp(shape(d.curve, raw), 0.0, 1.0);
        f += (d.amount / 100.0) * shaped;
    }
    return f;
}

// Builds a snapshot from explicit latch readings (caller draws fuzzyDab01
// from the stroke RNG only when a drive needs it).
inline SensorState makeSensorState(double pressure, double hold,
                                   double tiltXDeg, double tiltYDeg,
                                   double speed01, double distPx,
                                   double timeSec, double fuzzyDab01,
                                   double fuzzyStroke01, double barrelDeg,
                                   double tangential01, bool tabletDown) {
    SensorState st;
    st.pressure = pressure;
    st.hold = hold;
    st.tiltXDeg = tiltXDeg;
    st.tiltYDeg = tiltYDeg;
    st.speed01 = speed01;
    st.distPx = distPx;
    st.timeSec = timeSec;
    st.fuzzyDab01 = fuzzyDab01;
    st.fuzzyStroke01 = fuzzyStroke01;
    double b = std::fmod(barrelDeg, 360.0);
    if (b < 0.0) b += 360.0;
    st.barrel01 = b / 360.0;
    st.tangential01 = tangential01;
    st.tabletDown = tabletDown;
    return st;
}

// True when any drive for `prop` needs a per-dab random draw.
inline bool needsFuzzyDab(const std::vector<SensorDrive>& drives,
                          const std::string& prop) {
    for (const auto& d : drives) {
        if (d.prop == prop && d.amount != 0.0 &&
            d.sensor == Sensor::FuzzyDab)
            return true;
    }
    return false;
}

}  // namespace pittore::ui::sensordrive
