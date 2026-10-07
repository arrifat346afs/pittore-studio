#include "ui/persona/vector_qr.h"

#include <QRectF>

#include <algorithm>
#include <cmath>

#include "engine/vector/vector_art.h"

#ifdef HAVE_QRENCODE
#include <qrencode.h>
#endif

namespace pittore::ui {

std::shared_ptr<pittore::vector::ArtNode> makeQrArt(const QString& content,
                                                    int sizePx, int eccLevel) {
#ifdef HAVE_QRENCODE
    if (content.isEmpty() || sizePx < 21) return nullptr;
    const QRectF frame(0, 0, sizePx, sizePx);
    const QByteArray bytes = content.toUtf8();
    const QRecLevel level = (eccLevel <= 0)   ? QR_ECLEVEL_L
                            : (eccLevel == 1) ? QR_ECLEVEL_M
                            : (eccLevel == 2) ? QR_ECLEVEL_Q
                                              : QR_ECLEVEL_H;
    QRcode* code =
        QRcode_encodeString(bytes.constData(), 0, level, QR_MODE_8, 1);
    if (!code || code->width < 21) {
        if (code) QRcode_free(code);
        return nullptr;
    }
    const int modules = code->width;
    const double cell = frame.width() / modules;
    auto node = std::make_shared<pittore::vector::ArtNode>();
    node->name = "QR Code";
    node->segments.reserve(static_cast<std::size_t>(modules * modules));
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if ((code->data[y * modules + x] & 1) == 0) continue;
            const float x0 = static_cast<float>(x * cell);
            const float y0 = static_cast<float>(y * cell);
            const float x1 = static_cast<float>((x + 1) * cell);
            const float y1 = static_cast<float>((y + 1) * cell);
            pittore::vector::Segment m, l1, l2, l3, c;
            m.kind = pittore::vector::Segment::Kind::MoveTo;
            m.x = x0;
            m.y = y0;
            l1.kind = l2.kind = l3.kind =
                pittore::vector::Segment::Kind::LineTo;
            l1.x = x1;
            l1.y = y0;
            l2.x = x1;
            l2.y = y1;
            l3.x = x0;
            l3.y = y1;
            c.kind = pittore::vector::Segment::Kind::Close;
            node->segments.push_back(m);
            node->segments.push_back(l1);
            node->segments.push_back(l2);
            node->segments.push_back(l3);
            node->segments.push_back(c);
        }
    }
    QRcode_free(code);
    if (node->segments.empty()) return nullptr;
    node->paint.hasFill = true;
    node->paint.fill[0] = node->paint.fill[1] = node->paint.fill[2] = 0;
    node->paint.fill[3] = 255;
    return node;
#else
    (void)content;
    (void)sizePx;
    (void)eccLevel;
    return nullptr;
#endif
}

}  // namespace pittore::ui
