#pragma once
// Profiled CMYK -> working sRGB (clean-room implementation).
//
// Wraps one LCMS2 CMYK->sRGB transform (relative colorimetric + black-point
// compensation, the photo import default) built from embedded ICC bytes.
// 16-bit CMYK input (ink amount): the float CMYK path misbehaves on some
// LCMS builds, while 16-bit is verified correct — 1/65535 quantizing is
// invisible. Construction self-tests full black ink and refuses
// output-direction-only profiles (which map everything near white) instead
// of bleaching an import.
// Without a usable profile — empty bytes, corrupt data, or a build without
// LCMS2 — every call falls back to the naive core (convert.h), so decode
// paths never branch on availability. One instance per decode (never shared
// across threads); the pimpl keeps lcms2 out of every includer's compile.

#include <cstddef>
#include <cstdint>

namespace pittore::color {

class CmykToSrgb {
  public:
    explicit CmykToSrgb(const std::uint8_t* icc, std::size_t n);
    ~CmykToSrgb();
    CmykToSrgb(const CmykToSrgb&) = delete;
    CmykToSrgb& operator=(const CmykToSrgb&) = delete;

    // True when the profiled path is live (LCMS2 build + parsable profile).
    bool valid() const;

    // True ink coverage 0..1 in, straight sRGB light 0..1 out (clamped).
    void convert(float c, float m, float y, float k, float* rgb) const;

  private:
    struct Impl;
    Impl* impl_;
};

// Profiled working sRGB -> CMYK separation: the encode direction used by
// CMYK export and Image > Mode > CMYK (clean-room, same LCMS2 discipline as
// CmykToSrgb above). One transform, device-domain 16-bit on both sides
// (TYPE_RGB_16 -> TYPE_CMYK_16) so the float and batch entry points share it.
// Self-tests that paper white separates to ~no ink and sRGB black to a
// substantial ink lay, refusing profiles whose PCS->device tables are
// missing or garbage; without LCMS2 or a usable profile every call falls
// back to the naive full-GCR core (convert.h) so callers never branch.
class SrgbToCmyk {
  public:
    explicit SrgbToCmyk(const std::uint8_t* icc, std::size_t n);
    ~SrgbToCmyk();
    SrgbToCmyk(const SrgbToCmyk&) = delete;
    SrgbToCmyk& operator=(const SrgbToCmyk&) = delete;

    // True when the profiled path is live (LCMS2 build + parsable profile).
    bool valid() const;

    // Straight sRGB light 0..1 in, ink coverage 0..1 out (clamped).
    void convert(float r, float g, float b, float* cmyk) const;

    // Device-domain batch path: `pixels` * 3 RGB16 samples in, `pixels` *
    // 4 ink16 samples out (interleaved C,M,Y,K). Identical maths to
    // convert(); exists so big exports pay the transform call once.
    void convert16(const std::uint16_t* rgb, std::uint16_t* cmyk,
                   std::size_t pixels) const;

  private:
    struct Impl;
    Impl* impl_;
};

}  // namespace pittore::color
