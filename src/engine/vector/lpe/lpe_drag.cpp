// Drag-derived LPE params shared by canvas gestures and the panel: the
// dialog defaults + on-canvas handle mapping rolled into one deterministic
// function (same drag + amount → same effect, everywhere).
#include "engine/vector/lpe/lpe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pittore::vector::lpe {

Params dragParamsFor(EffectType type, double x0, double y0, double x1, double y1,
                     double amountPct) {
    Params p;
    const double drag =
        std::max(1.0, std::hypot(x1 - x0, y1 - y0));
    const double k = amountPct / 100.0;
    switch (type) {
        case EffectType::Offset:
            p.setDouble("amount", drag * k);
            break;
        case EffectType::FilletChamfer:
            p.setDouble("radius", drag * k);
            break;
        case EffectType::Simplify:
            p.setDouble("threshold", std::max(0.5, drag * k / 10));
            break;
        case EffectType::BendPath:
            p.setDouble("bend", drag * k / 2);
            p.setDouble("down", (y1 - y0) >= 0 ? 1.0 : -1.0);
            break;
        case EffectType::Envelope:
            p.setDouble("ex", drag * k / 200);
            p.setDouble("ey", drag * k / 200);
            break;
        case EffectType::Roughen:
        case EffectType::Sketch:
            p.setDouble("amount", drag * k / 50);
            break;
        case EffectType::TaperStroke:
        case EffectType::Powerstroke:
            p.setDouble("width", std::max(1.0, drag * k / 20));
            break;
        case EffectType::CopyRotate: {
            int copies = std::min(24, std::max(2, 2 + (int)(drag / 30)));
            p.setDouble("copies", copies);
            p.setDouble("angle", 360.0 / copies);
            break;
        }
        case EffectType::MirrorSymmetry:
            p.setDouble("gap", x0);  // mirror axis through the press
            break;
        case EffectType::Tiling: {
            int copies = std::min(16, std::max(2, 2 + (int)(drag / 40)));
            p.setDouble("copies", copies);
            p.setDouble("cols", 3);
            p.setDouble("gap", std::max(8.0, drag / 4));
            break;
        }
        case EffectType::Gears:
            p.setDouble("teeth", std::min(48, std::max(6, 6 + (int)(drag / 12))));
            p.setDouble("radius", std::max(8.0, drag / 2));
            break;
        case EffectType::Extrude:
            p.setDouble("dx", x1 - x0);
            p.setDouble("dy", y1 - y0);
            break;
        case EffectType::MeasureSegments:
            p.setDouble("step", std::max(4.0, drag * k / 20));
            break;
        case EffectType::Powerclip: {
            char b[128];
            snprintf(b, sizeof(b), "%g,%g,%g,%g", std::min(x0, x1), std::min(y0, y1),
                     std::abs(x1 - x0), std::abs(y1 - y0));
            p.set("rect", b);
            break;
        }
        default:
            break;
    }
    return p;
}

}  // namespace pittore::vector::lpe
