#pragma once
// QR Code art (ui/persona): libqrencode-backed module grid committed as
// filled vector squares. Without the library (HAVE_QRENCODE unset),
// makeQrArt returns null and creation keeps its honest refusal.
#include <QString>

#include <memory>

namespace pittore::vector {
struct ArtNode;
}

namespace pittore::ui {

// Byte-mode QR in a `sizePx` local frame. `eccLevel` 0..3 = L/M/Q/H.
// Null on empty content, encode failure, or a missing backend.
std::shared_ptr<pittore::vector::ArtNode> makeQrArt(const QString& content,
                                                    int sizePx, int eccLevel);

}  // namespace pittore::ui
