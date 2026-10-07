#pragma once
// Soft-proof manager over LittleCMS2. Host-side only: document RGB floats in,
// display RGB floats out. Lives outside the compute backends on purpose — the
// proof runs on the CPU after readback, so no kernel changes on any backend.
//
// Threading: one ProofManager per worker thread. Each instance owns its LCMS
// context; instances must not be shared across threads.
//
// Without LCMS2 (-DPITTORE_LCMS2 unset) available() is false and every call
// reports it; callers stay compiled so builds without lcms2 keep working.

#include <cstddef>
#include <cstdint>
#include <string>

namespace pittore::color {

enum class ProofIntent {
    Perceptual = 0,
    Relative = 1,  // photo default, pair with black-point compensation
    Saturation = 2,
    Absolute = 3,  // paper simulation only
};

class ProofManager {
  public:
    ProofManager();
    ~ProofManager();
    ProofManager(const ProofManager&) = delete;
    ProofManager& operator=(const ProofManager&) = delete;

    bool available() const;

    // Document profile. Default is the built-in sRGB; call only to override.
    // "" clears back to built-in sRGB. Returns "" on success, reason if not.
    std::string setSourceProfile(const std::string& path);

    // Proof (printer/output) profile. Empty until set; applyProof() without
    // one returns an error. Returns "" on success, reason if not.
    std::string setProofProfile(const std::string& path);

    void setIntent(ProofIntent intent, ProofIntent proofIntent);
    void setBlackPointCompensation(bool on);
    // Gamut alarm color as 0..1 RGB shown for out-of-proof-gamut pixels.
    void setGamutCheck(bool on, float alarmR = 1.0f, float alarmG = 0.0f,
                       float alarmB = 1.0f);

    // Runs the cached document -> proof -> display transform over w*h
    // straight-alpha RGBA floats. Alpha passes through untouched.
    // Display is the built-in sRGB. Returns "" on success, reason if not.
    // src and dst may alias.
    std::string applyProof(const float* src, float* dst, std::uint32_t w,
                           std::uint32_t h);

    // Number of live cached transforms (0 or 1; kept for the cache-key test).
    std::size_t cacheSize() const;

    struct Impl;  // defined in proof.cpp; named here so its helpers compile
  private:
    Impl* impl_;
};

}  // namespace pittore::color
