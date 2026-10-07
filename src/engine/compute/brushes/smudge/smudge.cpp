#include "engine/compute/brushes/smudge/smudge.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/core/pixel.h"

namespace pittore::compute {
namespace {

float sample_alpha(const float* p, std::uint32_t w, std::uint32_t h, float u,
                   float v, bool bilinear = true) {
    if (u < 0.0f || v < 0.0f || u > float(w) - 1.0f || v > float(h) - 1.0f)
        return 0.0f;
    if (!bilinear) {
        const std::uint32_t xi = std::min(static_cast<std::uint32_t>(u + 0.5f),
                                          w - 1);
        const std::uint32_t yi = std::min(static_cast<std::uint32_t>(v + 0.5f),
                                          h - 1);
        return p[std::size_t(yi) * w + xi];
    }
    const std::uint32_t x0 = static_cast<std::uint32_t>(u);
    const std::uint32_t y0 = static_cast<std::uint32_t>(v);
    const std::uint32_t x1 = x0 + 1 < w ? x0 + 1 : x0;
    const std::uint32_t y1 = y0 + 1 < h ? y0 + 1 : y0;
    const float fx = u - float(x0), fy = v - float(y0);
    const float a = p[std::size_t(y0) * w + x0];
    const float b = p[std::size_t(y0) * w + x1];
    const float c = p[std::size_t(y1) * w + x0];
    const float d = p[std::size_t(y1) * w + x1];
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

RGBAf sample_src(const RGBAf* dst, std::uint32_t w, std::uint32_t h, int x,
                 int y, const SmudgePick* pick) {
    if (pick && pick->data && pick->w > 0 && pick->h > 0) {
        const int px = std::clamp(x - static_cast<int>(pick->ox), 0,
                                  static_cast<int>(pick->w) - 1);
        const int py = std::clamp(y - static_cast<int>(pick->oy), 0,
                                  static_cast<int>(pick->h) - 1);
        return pick->data[std::size_t(py) * pick->w +
                          static_cast<std::size_t>(px)];
    }
    const int sx = std::clamp(x, 0, static_cast<int>(w) - 1);
    const int sy = std::clamp(y, 0, static_cast<int>(h) - 1);
    return dst[std::size_t(sy) * w + sx];
}

// Prime or resize the travelling patch to `side` (odd, >=1) centred at
// (ccx, ccy). Finger starts from fg; otherwise the patch copies the pickup
// source under the dab. A size change resamples the old patch (nearest) so
// pressure-driven radius wobble never drops the loaded paint.
void ensure_carry(SmudgeCarry& carry, int side, int ccx, int ccy,
                  const RGBAf* dst, std::uint32_t w, std::uint32_t h,
                  const SmudgePick* pick, const SmudgeCtl* ctl,
                  const float* height) {
    if (side < 1) side = 1;
    if (side % 2 == 0) ++side;
    const bool wantHeight = height != nullptr;
    if (!carry.empty() && carry.side == side && carry.hasHeight == wantHeight)
        return;
    std::vector<RGBAf> nextColor(std::size_t(side) * side);
    std::vector<float> nextHeight;
    if (wantHeight) nextHeight.assign(std::size_t(side) * side, 0.0f);
    const int r = side / 2;
    const bool finger = ctl && ctl->fingerPaint;
    const RGBAf fg = ctl ? ctl->fg : RGBAf{0, 0, 0, 0};
    if (carry.empty()) {
        for (int qy = 0; qy < side; ++qy) {
            for (int qx = 0; qx < side; ++qx) {
                const int x = ccx - r + qx;
                const int y = ccy - r + qy;
                RGBAf& out = nextColor[std::size_t(qy) * side + qx];
                out = finger ? RGBAf{fg.r, fg.g, fg.b, 1.0f}
                             : sample_src(dst, w, h, x, y, pick);
                if (wantHeight) {
                    const int hx = std::clamp(x, 0, static_cast<int>(w) - 1);
                    const int hy = std::clamp(y, 0, static_cast<int>(h) - 1);
                    nextHeight[std::size_t(qy) * side + qx] =
                        height[std::size_t(hy) * w + hx];
                }
            }
        }
    } else {
        const int oldSide = carry.side;
        const int oldR = oldSide / 2;
        (void)oldR;
        for (int qy = 0; qy < side; ++qy) {
            for (int qx = 0; qx < side; ++qx) {
                const int ox =
                    std::clamp(qx * oldSide / side, 0, oldSide - 1);
                const int oy =
                    std::clamp(qy * oldSide / side, 0, oldSide - 1);
                nextColor[std::size_t(qy) * side + qx] =
                    carry.color[std::size_t(oy) * oldSide + ox];
                if (wantHeight) {
                    float hv = 0.0f;
                    if (carry.hasHeight &&
                        carry.height.size() ==
                            std::size_t(oldSide) * oldSide)
                        hv = carry.height[std::size_t(oy) * oldSide + ox];
                    else if (height) {
                        const int x = ccx - r + qx;
                        const int y = ccy - r + qy;
                        const int hx =
                            std::clamp(x, 0, static_cast<int>(w) - 1);
                        const int hy =
                            std::clamp(y, 0, static_cast<int>(h) - 1);
                        hv = height[std::size_t(hy) * w + hx];
                    }
                    nextHeight[std::size_t(qy) * side + qx] = hv;
                }
            }
        }
    }
    carry.color = std::move(nextColor);
    carry.height = std::move(nextHeight);
    carry.side = side;
    carry.hasHeight = wantHeight;
}

// Shared two-pass smudge: `cover(x, y)` returns the dab coverage in [0,1].
// `pick` (when set) replaces the dab target as the pickup source;
// `blend` constrains the smear through a blend mode (Normal = plain smear).
template <typename CoverFn>
void smudge_pass(RGBAf* dst, std::uint32_t w, std::uint32_t h, float cx,
                 float cy, float radius, float rate, CoverFn&& cover,
                 SmudgeCarry& carry, const SelectionMask* selection,
                 const PatternTex* tex, const DabDensity* den,
                 const MaskTip* mask, const SmudgePick* pick,
                 BlendMode blend, const SmudgeCtl* ctl,
                 float* height) {
    if (!dst || radius <= 0.0f || rate <= 0.0f || w == 0 || h == 0) return;
    const float r = std::clamp(rate, 0.0f, 1.0f);
    const bool smear = ctl && ctl->mode == SmudgeMode::Smear;
    const int trailXi = smear ? int(std::lround(ctl ? ctl->trailX : 0.0f)) : 0;
    const int trailYi = smear ? int(std::lround(ctl ? ctl->trailY : 0.0f)) : 0;
    const float colorRate =
        ctl ? std::clamp(ctl->colorRate, 0.0f, 1.0f) : 0.0f;
    const RGBAf fg = ctl ? ctl->fg : RGBAf{0, 0, 0, 0};
    const int side = std::max(1, 2 * int(std::ceil(radius)) + 1);
    const int pr = side / 2;
    const int ccx = int(std::lround(cx));
    const int ccy = int(std::lround(cy));
    ensure_carry(carry, side, ccx, ccy, dst, w, h, pick, ctl, height);
    // Foreground reload rides the current dab: the patch leans toward fg
    // before it touches the canvas, so the stroke lays color immediately
    // instead of one dab late.
    if (colorRate > 0.0f) {
        for (auto& c : carry.color) {
            c.r += (fg.r - c.r) * colorRate;
            c.g += (fg.g - c.g) * colorRate;
            c.b += (fg.b - c.b) * colorRate;
        }
    }
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius)));
    const int y1 = std::min(static_cast<int>(h) - 1,
                            static_cast<int>(std::ceil(cy + radius)));
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius)));
    const int x1 = std::min(static_cast<int>(w) - 1,
                            static_cast<int>(std::ceil(cx + radius)));
    // Pass 1: lay the carried patch onto the canvas (premultiplied, so
    // transparent paint never drags dark fringes with it).
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float cov = cover(float(x), float(y));
            if (cov <= 0.0f) continue;
            cov = combine_mask(cov, mask, float(x) + 0.5f - cx,
                               float(y) + 0.5f - cy, radius);
            if (selection) cov *= selection->coverage(x, y);
            if (tex) cov *= texture_mult(tex, float(x), float(y));
            if (cov <= 0.0f || !density_keep(den, x, y)) continue;
            const float a = std::clamp(cov * r, 0.0f, 1.0f);
            if (a <= 0.0f) continue;
            const int qx = x - ccx + pr;
            const int qy = y - ccy + pr;
            if (qx < 0 || qy < 0 || qx >= side || qy >= side) continue;
            const RGBAf& src =
                carry.color[std::size_t(qy) * side + qx];
            RGBAf& d = dst[std::size_t(y) * w + x];
            const float inv = 1.0f - a;
            const float outA = a * src.a + d.a * inv;
            if (outA > 1e-6f) {
                const float sr =
                    (a * src.r * src.a + d.r * d.a * inv) / outA;
                const float sg =
                    (a * src.g * src.a + d.g * d.a * inv) / outA;
                const float sb =
                    (a * src.b * src.a + d.b * d.a * inv) / outA;
                if (blend == BlendMode::Normal) {
                    d.r = sr;
                    d.g = sg;
                    d.b = sb;
                } else {
                    float or_, og, ob, oa;
                    blend::pixel(blend, sr, sg, sb, outA, d.r, d.g, d.b,
                                 d.a, x, y, or_, og, ob, oa);
                    d.r = or_;
                    d.g = og;
                    d.b = ob;
                }
                d.a = outA;
            } else if (outA <= 1e-6f) {
                d.r = 0.0f;
                d.g = 0.0f;
                d.b = 0.0f;
                d.a = 0.0f;
            }
            if (height && carry.hasHeight &&
                carry.height.size() == carry.color.size()) {
                float& hd = height[std::size_t(y) * w + x];
                hd += (carry.height[std::size_t(qy) * side + qx] - hd) * a;
            }
        }
    }
    // Pass 2: reload the patch from the canvas it just touched. `keep`
    // follows the strength: a hard stroke drags its load far, a soft one
    // adapts to the canvas within a dab or two — but even a full-strength
    // stroke keeps a little pickup, so the dirty brush always dilutes.
    // Smear samples from behind the stroke direction; dulling samples under
    // the dab.
    float keep = r;
    float take = 1.0f - keep;
    if (take < 0.15f) {
        take = 0.15f;
        keep = 1.0f - take;
    }
    if (take <= 0.0f) return;
    for (int qy = 0; qy < side; ++qy) {
        for (int qx = 0; qx < side; ++qx) {
            const int x = ccx - pr + qx - trailXi;
            const int y = ccy - pr + qy - trailYi;
            const RGBAf s = sample_src(dst, w, h, x, y, pick);
            RGBAf& c = carry.color[std::size_t(qy) * side + qx];
            {
                const float caPmR = c.r * c.a;
                const float caPmG = c.g * c.a;
                const float caPmB = c.b * c.a;
                const float sPmR = s.r * s.a;
                const float sPmG = s.g * s.a;
                const float sPmB = s.b * s.a;
                const float outA = c.a * keep + s.a * take;
                if (outA > 1e-6f) {
                    c.r = (caPmR * keep + sPmR * take) / outA;
                    c.g = (caPmG * keep + sPmG * take) / outA;
                    c.b = (caPmB * keep + sPmB * take) / outA;
                    c.a = std::clamp(outA, 0.0f, 1.0f);
                } else {
                    c.r = c.g = c.b = c.a = 0.0f;
                }
            }
            if (height && carry.hasHeight &&
                carry.height.size() == carry.color.size()) {
                const int hx = std::clamp(x, 0, static_cast<int>(w) - 1);
                const int hy = std::clamp(y, 0, static_cast<int>(h) - 1);
                const float hs = height[std::size_t(hy) * w + hx];
                float& hc = carry.height[std::size_t(qy) * side + qx];
                hc = hc * keep + hs * take;
            }
        }
    }
}

}  // namespace

void smudge_tip_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                         float cx, float cy, float radius, const AutoTip& tip,
                         float rate, float radiusFrac, SmudgeCarry& carry,
                         const SelectionMask* selection,
                         const PatternTex* tex, const DabDensity* den,
                         const MaskTip* mask, int flip,
                         const SmudgePick* pick, BlendMode blend,
                         const SmudgeCtl* ctl, float* height) {
    (void)flip;
    AutoTip t = tip;
    t.sanitize();
    const float rad = t.angleDeg * 3.14159265358979323846f / 180.0f;
    const float cosA = std::cos(rad), sinA = std::sin(rad);
    const float invRatio = 1.0f / t.ratio;
    const float rr = radius * std::clamp(radiusFrac, 0.05f, 1.0f);
    if (rr <= 0.0f) return;
    const float inv_r = 1.0f / rr;
    smudge_pass(dst, w, h, cx, cy, rr, rate,
                [&](float x, float y) {
                    const float dx = x + 0.5f - cx;
                    const float dy = y + 0.5f - cy;
                    return auto_tip_coverage(dx, dy, inv_r, t, cosA, sinA,
                                             invRatio);
                },
                carry, selection, tex, den, mask, pick, blend, ctl,
                height);
}

void smudge_stamp_dab_host(RGBAf* dst, std::uint32_t w, std::uint32_t h,
                           float cx, float cy, float radius,
                           const StampTip& tip, float rate, float radiusFrac,
                           float angleDeg, SmudgeCarry& carry,
                           const SelectionMask* selection,
                           const PatternTex* tex, const DabDensity* den,
                           const MaskTip* mask, int flip, int filter,
                           const SmudgePick* pick, BlendMode blend,
                           const SmudgeCtl* ctl, float* height) {
    if (!tip.valid()) return;
    const float rad = angleDeg * 3.14159265358979323846f / 180.0f;
    const float cosA = std::cos(rad), sinA = std::sin(rad);
    const float longest = float(tip.w > tip.h ? tip.w : tip.h);
    const float rr = radius * std::clamp(radiusFrac, 0.05f, 1.0f);
    if (rr <= 0.0f) return;
    const float scale = longest / (2.0f * rr);
    const float cxTip = (float(tip.w) - 1.0f) * 0.5f;
    const float cyTip = (float(tip.h) - 1.0f) * 0.5f;
    smudge_pass(dst, w, h, cx, cy, rr, rate,
                [&](float x, float y) {
                    const float dx = x + 0.5f - cx;
                    const float dy = y + 0.5f - cy;
                    const float uc = (dx * cosA - dy * sinA) * scale;
                    const float vc = (dx * sinA + dy * cosA) * scale;
                    const float u = (flip & 1 ? -uc : uc) + cxTip;
                    const float v = (flip & 2 ? -vc : vc) + cyTip;
                    return std::clamp(sample_alpha(tip.alpha.data(), tip.w,
                                                  tip.h, u, v,
                                                  filter != 0),
                                      0.0f, 1.0f);
                },
                carry, selection, tex, den, mask, pick, blend, ctl,
                height);
}

}  // namespace pittore::compute
