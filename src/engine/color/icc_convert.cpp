// Clean-room LCMS2 import transform. Verified against the system LCMS2:
// 16-bit CMYK input (ink amount, 65535 = full ink) converts correctly while
// the float CMYK input path misbehaves on this build (everything maps near
// white), so the converter quantizes to 16-bit — 1/65535 is far below any
// visible threshold. Output is float sRGB.

#include "engine/color/icc_convert.h"

#include <algorithm>
#include <cmath>

#include "engine/core/pixel.h"

#ifdef PITTORE_LCMS2
#include <lcms2.h>
#endif

namespace pittore::color {

struct CmykToSrgb::Impl {
    bool ok = false;
#ifdef PITTORE_LCMS2
    cmsHPROFILE src = nullptr;
    cmsHPROFILE dst = nullptr;
    cmsHTRANSFORM xform = nullptr;
#endif
};

CmykToSrgb::CmykToSrgb(const std::uint8_t* icc, std::size_t n)
    : impl_(new Impl()) {
#ifdef PITTORE_LCMS2
    if (!icc || n < 132 || n > (1u << 24)) return;
    impl_->src = cmsOpenProfileFromMem(icc, static_cast<cmsUInt32Number>(n));
    if (!impl_->src) return;
    if (cmsGetColorSpace(impl_->src) != cmsSigCmykData) {
        cmsCloseProfile(impl_->src);
        impl_->src = nullptr;
        return;
    }
    impl_->dst = cmsCreate_sRGBProfile();
    if (!impl_->dst) {
        cmsCloseProfile(impl_->src);
        impl_->src = nullptr;
        return;
    }
    impl_->xform = cmsCreateTransform(
        impl_->src, TYPE_CMYK_16, impl_->dst, TYPE_RGB_FLT,
        INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_BLACKPOINTCOMPENSATION);
    if (!impl_->xform) {
        cmsCloseProfile(impl_->src);
        cmsCloseProfile(impl_->dst);
        impl_->src = impl_->dst = nullptr;
        return;
    }
    // Self-test: full black ink must come out dark. Profiles without a
    // usable device-to-PCS direction (output-only tables) map everything
    // near white instead of failing — refuse those rather than bleaching
    // an import.
    {
        const cmsUInt16Number black[4] = {0, 0, 0, 65535};
        float out[3] = {1.0f, 1.0f, 1.0f};
        cmsDoTransform(impl_->xform, black, out, 1);
        if (out[0] > 0.9f && out[1] > 0.9f && out[2] > 0.9f) {
            cmsDeleteTransform(impl_->xform);
            cmsCloseProfile(impl_->src);
            cmsCloseProfile(impl_->dst);
            impl_->xform = nullptr;
            impl_->src = impl_->dst = nullptr;
            return;
        }
    }
    impl_->ok = true;
#else
    (void)icc;
    (void)n;
#endif
}

CmykToSrgb::~CmykToSrgb() {
#ifdef PITTORE_LCMS2
    if (impl_->xform) cmsDeleteTransform(impl_->xform);
    if (impl_->src) cmsCloseProfile(impl_->src);
    if (impl_->dst) cmsCloseProfile(impl_->dst);
#endif
    delete impl_;
}

bool CmykToSrgb::valid() const { return impl_->ok; }

void CmykToSrgb::convert(float c, float m, float y, float k,
                         float* rgb) const {
    c = std::clamp(c, 0.0f, 1.0f);
    m = std::clamp(m, 0.0f, 1.0f);
    y = std::clamp(y, 0.0f, 1.0f);
    k = std::clamp(k, 0.0f, 1.0f);
#ifdef PITTORE_LCMS2
    if (impl_->ok) {
        const cmsUInt16Number ink[4] = {
            static_cast<cmsUInt16Number>(std::lround(c * 65535.0f)),
            static_cast<cmsUInt16Number>(std::lround(m * 65535.0f)),
            static_cast<cmsUInt16Number>(std::lround(y * 65535.0f)),
            static_cast<cmsUInt16Number>(std::lround(k * 65535.0f))};
        float out[3] = {0.0f, 0.0f, 0.0f};
        cmsDoTransform(impl_->xform, ink, out, 1);
        rgb[0] = std::clamp(out[0], 0.0f, 1.0f);
        rgb[1] = std::clamp(out[1], 0.0f, 1.0f);
        rgb[2] = std::clamp(out[2], 0.0f, 1.0f);
        return;
    }
#endif
    // Naive fallback (same math as convert.h, inlined to keep this TU
    // dependency-free): r = (1-c)(1-k).
    const float base = 1.0f - k;
    rgb[0] = (1.0f - c) * base;
    rgb[1] = (1.0f - m) * base;
    rgb[2] = (1.0f - y) * base;
}

struct SrgbToCmyk::Impl {
    bool ok = false;
#ifdef PITTORE_LCMS2
    cmsHPROFILE src = nullptr;
    cmsHPROFILE dst = nullptr;
    cmsHTRANSFORM xform = nullptr;
#endif
};

SrgbToCmyk::SrgbToCmyk(const std::uint8_t* icc, std::size_t n)
    : impl_(new Impl()) {
#ifdef PITTORE_LCMS2
    if (!icc || n < 132 || n > (1u << 24)) return;
    impl_->dst = cmsOpenProfileFromMem(icc, static_cast<cmsUInt32Number>(n));
    if (!impl_->dst) return;
    if (cmsGetColorSpace(impl_->dst) != cmsSigCmykData) {
        cmsCloseProfile(impl_->dst);
        impl_->dst = nullptr;
        return;
    }
    impl_->src = cmsCreate_sRGBProfile();
    if (!impl_->src) {
        cmsCloseProfile(impl_->dst);
        impl_->dst = nullptr;
        return;
    }
    impl_->xform = cmsCreateTransform(
        impl_->src, TYPE_RGB_16, impl_->dst, TYPE_CMYK_16,
        INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_BLACKPOINTCOMPENSATION);
    if (!impl_->xform) {
        cmsCloseProfile(impl_->src);
        cmsCloseProfile(impl_->dst);
        impl_->src = impl_->dst = nullptr;
        return;
    }
    // Self-test both ends: paper white must separate to ~no ink (a profile
    // without a usable PCS->device direction maps white to a full ink lay)
    // and sRGB black must carry a substantial lay (a table that swallows
    // shadows fails the other way). Refuse either instead of exporting a
    // separation that is wrong end to end.
    {
        const cmsUInt16Number white[3] = {65535, 65535, 65535};
        const cmsUInt16Number black[3] = {0, 0, 0};
        cmsUInt16Number w[4] = {65535, 65535, 65535, 65535};
        cmsUInt16Number b[4] = {0, 0, 0, 0};
        cmsDoTransform(impl_->xform, white, w, 1);
        cmsDoTransform(impl_->xform, black, b, 1);
        const unsigned wSum = static_cast<unsigned>(w[0]) + w[1] + w[2] + w[3];
        const unsigned bSum = static_cast<unsigned>(b[0]) + b[1] + b[2] + b[3];
        if (wSum > 65535u / 6u || bSum < 65535u * 2u / 5u) {
            cmsDeleteTransform(impl_->xform);
            cmsCloseProfile(impl_->src);
            cmsCloseProfile(impl_->dst);
            impl_->xform = nullptr;
            impl_->src = impl_->dst = nullptr;
            return;
        }
    }
    impl_->ok = true;
#else
    (void)icc;
    (void)n;
#endif
}

SrgbToCmyk::~SrgbToCmyk() {
#ifdef PITTORE_LCMS2
    if (impl_->xform) cmsDeleteTransform(impl_->xform);
    if (impl_->src) cmsCloseProfile(impl_->src);
    if (impl_->dst) cmsCloseProfile(impl_->dst);
#endif
    delete impl_;
}

bool SrgbToCmyk::valid() const { return impl_->ok; }

void SrgbToCmyk::convert(float r, float g, float b, float* cmyk) const {
    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
    const std::uint16_t rgb[3] = {
        static_cast<std::uint16_t>(std::lround(r * 65535.0f)),
        static_cast<std::uint16_t>(std::lround(g * 65535.0f)),
        static_cast<std::uint16_t>(std::lround(b * 65535.0f))};
    std::uint16_t ink[4] = {0, 0, 0, 0};
    convert16(rgb, ink, 1);
    cmyk[0] = static_cast<float>(ink[0]) / 65535.0f;
    cmyk[1] = static_cast<float>(ink[1]) / 65535.0f;
    cmyk[2] = static_cast<float>(ink[2]) / 65535.0f;
    cmyk[3] = static_cast<float>(ink[3]) / 65535.0f;
}

void SrgbToCmyk::convert16(const std::uint16_t* rgb, std::uint16_t* cmyk,
                           std::size_t pixels) const {
    if (pixels == 0) return;
#ifdef PITTORE_LCMS2
    if (impl_->ok) {
        cmsDoTransform(impl_->xform, rgb, cmyk, static_cast<cmsUInt32Number>(pixels));
        return;
    }
#endif
    // Naive full-GCR fallback (same math as convert.h): k = 1 - max(r,g,b).
    for (std::size_t i = 0; i < pixels; ++i) {
        const float r = static_cast<float>(rgb[i * 3 + 0]) / 65535.0f;
        const float g = static_cast<float>(rgb[i * 3 + 1]) / 65535.0f;
        const float b = static_cast<float>(rgb[i * 3 + 2]) / 65535.0f;
        const float k = 1.0f - std::max({r, g, b});
        float c = 0.0f, m = 0.0f, y = 0.0f;
        if (k < 1.0f) {
            const float inv = 1.0f / (1.0f - k);
            c = (1.0f - r - k) * inv;
            m = (1.0f - g - k) * inv;
            y = (1.0f - b - k) * inv;
        }
        cmyk[i * 4 + 0] =
            static_cast<std::uint16_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 65535.0f));
        cmyk[i * 4 + 1] =
            static_cast<std::uint16_t>(std::lround(std::clamp(m, 0.0f, 1.0f) * 65535.0f));
        cmyk[i * 4 + 2] =
            static_cast<std::uint16_t>(std::lround(std::clamp(y, 0.0f, 1.0f) * 65535.0f));
        cmyk[i * 4 + 3] =
            static_cast<std::uint16_t>(std::lround(std::clamp(k, 0.0f, 1.0f) * 65535.0f));
    }
}

}  // namespace pittore::color
