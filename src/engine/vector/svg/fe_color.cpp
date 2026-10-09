// Matrix is 4 rows of 5 values.
#include "engine/vector/svg/fe_color.h"

namespace pittore::svg {

FeImg feApplyMatrix(const FeImg& src, const std::vector<double>& m20) {
    FeImg out = src;
    if (m20.size() < 20) {
        return out;
    }
    for (size_t k = 0; k < src.px.size(); k += 4) {
        const double r = src.px[k], g = src.px[k + 1], b = src.px[k + 2],
                     a = src.px[k + 3];
        out.px[k] = (float)(m20[0] * r + m20[1] * g + m20[2] * b + m20[3] * a +
                            m20[4]);
        out.px[k + 1] = (float)(m20[5] * r + m20[6] * g + m20[7] * b +
                                m20[8] * a + m20[9]);
        out.px[k + 2] = (float)(m20[10] * r + m20[11] * g + m20[12] * b +
                                m20[13] * a + m20[14]);
        out.px[k + 3] = (float)(m20[15] * r + m20[16] * g + m20[17] * b +
                                m20[18] * a + m20[19]);
    }
    return out;
}

FeImg feLuminanceToAlpha(const FeImg& src) {
    FeImg out = src;
    for (size_t k = 0; k < src.px.size(); k += 4) {
        const float l = 0.2126f * src.px[k] + 0.7152f * src.px[k + 1] +
                        0.0722f * src.px[k + 2];
        out.px[k] = 0;
        out.px[k + 1] = 0;
        out.px[k + 2] = 0;
        out.px[k + 3] = l * src.px[k + 3];
    }
    return out;
}

}  // namespace pittore::svg
