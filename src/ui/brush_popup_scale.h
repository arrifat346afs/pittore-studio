#pragma once
// Brush popup fit-to-screen scale (ui/brush_popup_scale.h).
// Pure geometry: no widgets, unit-tested in test_chrome_state.
// The popup is built once at reference (mock) size; before each show the
// button measures the panel against the target screen and applies a uniform
// scale in [kMinPopupScale, 1]. Big screens stay exactly 1.0 (mock 1:1,
// no scrolling); small screens shrink fixed widths/heights/grid cells so
// the whole popup fits without clipping. Text sizes never change.
#include <algorithm>

namespace pittore::ui {

inline constexpr double kMinPopupScale = 0.6;

// needW/needH: panel totalSizeHint + menu chrome. availW/availH: screen
// available geometry. Budgets leave room for the options bar above and
// window margins: width gets 64 px, height 96 px of headroom.
inline double brushPopupScaleFor(double needW, double needH, double availW,
                                 double availH) {
    if (needW <= 0.0 || needH <= 0.0 || availW <= 0.0 || availH <= 0.0)
        return 1.0;
    const double budgetW = availW - 64.0;
    const double budgetH = availH - 96.0;
    if (budgetW <= 0.0 || budgetH <= 0.0) return kMinPopupScale;
    const double s =
        std::min({1.0, budgetW / needW, budgetH / needH});
    return std::clamp(s, kMinPopupScale, 1.0);
}

}  // namespace pittore::ui
