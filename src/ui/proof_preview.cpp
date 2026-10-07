// Clean-room QImage <-> straight-float bridge for the soft-proof path.
// Premultiplied ARGB32 is the composite's native format; the manager wants
// straight floats, so fully transparent texels become (0,0,0,0) and round
// back exactly, while Halb-transparency pays one 8-bit quantize each way —
// fine for a preview, never for pixels.

#include "ui/proof_preview.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/color/proof.h"

namespace pittore::ui {

QImage proofPreviewImage(const QImage& src,
                         pittore::color::ProofManager& pm) {
    if (src.isNull()) return {};
    const QImage argb =
        src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = argb.width(), h = argb.height();
    if (w <= 0 || h <= 0) return {};
    std::vector<float> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const auto* row =
            reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const unsigned a = qAlpha(row[x]);
            float* out = &px[(static_cast<std::size_t>(y) * w + x) * 4];
            if (a == 0) {
                out[0] = out[1] = out[2] = out[3] = 0.0f;
                continue;
            }
            const float inv = 255.0f / a;
            out[0] = qRed(row[x]) * inv / 255.0f;
            out[1] = qGreen(row[x]) * inv / 255.0f;
            out[2] = qBlue(row[x]) * inv / 255.0f;
            out[3] = a / 255.0f;
        }
    }
    if (!pm.applyProof(px.data(), px.data(), static_cast<std::uint32_t>(w),
                       static_cast<std::uint32_t>(h))
             .empty())
        return {};
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float* in = &px[(static_cast<std::size_t>(y) * w + x) * 4];
            const float a =
                std::clamp(in[3], 0.0f, 1.0f);
            const auto byte = [](float v) {
                return static_cast<int>(
                    std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
            };
            row[x] = qRgba(byte(in[0] * a), byte(in[1] * a), byte(in[2] * a),
                            byte(a));
        }
    }
    return out;
}

}  // namespace pittore::ui
