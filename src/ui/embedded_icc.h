#pragma once
// Embedded profile lookup shared by the import paths (File ▸ Open, drag &
// drop, Place). Clean-room, no dialog or policy logic here — this only answers
// "what profile does this file carry?".
//
// The file's own ICC block is asked first: it is what the file actually
// carries, so the dialog names the real profile (the decoded color space is
// the toolkit's reinterpretation — for a standard profile it answers with its
// own built-in label, which would both misname the dialog and skew the family
// comparison). When the container scan finds nothing — a format outside
// JPEG/PNG/WebP, or a profile with no readable 'desc' — the decoded image's
// color space answers instead, so a profile the toolkit can parse but whose
// bytes we did not walk is never mistaken for "nothing embedded". Every call
// reports which of the two answered, which is what the import log prints.

#include <QColorSpace>
#include <QFile>
#include <QImage>
#include <QString>

#include <string>

#include "engine/io/icc.h"
#include "engine/io/icc_scan.h"

namespace pittore::ui {

// Description of the profile embedded in `path`, or "" when the file carries
// none. `decoded` is the already-decoded image when one is at hand (pass a
// null QImage to skip it). `source`, when given, names the answer for the
// import log: "file container", "decoded color space", or "none".
inline QString embeddedProfileName(const QString& path, const QImage& decoded,
                                   QString* source = nullptr) {
    // QFile::encodeName matches what std::ifstream opens on every platform
    // the app ships on, so the scan sees the same bytes the decoder did.
    const QByteArray native = QFile::encodeName(path);
    const std::string fromFile =
        pittore::io::containerIccProfileName(native.constData());
    if (!fromFile.empty()) {
        if (source) *source = QStringLiteral("file container");
        return QString::fromStdString(fromFile);
    }
    if (!decoded.isNull()) {
        const QString fromDecode = decoded.colorSpace().description();
        if (!fromDecode.isEmpty()) {
            if (source) *source = QStringLiteral("decoded color space");
            return fromDecode;
        }
    }
    if (source) *source = QStringLiteral("none");
    return {};
}

}  // namespace pittore::ui
