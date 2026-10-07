// Image > Mode conversion (RGB / Grayscale / CMYK).
//
// Documents stay RGB-internal: every layer buffer is float appearance
// sRGB. "Convert to CMYK" therefore runs each colour through the
// separation and back (sRGB -> inks -> sRGB through one destination
// profile), so the canvas shows the gamut-reduced picture that will
// print, and keeps the profile bytes for export. Grayscale writes
// Rec.709 luma into all three channels — the same coefficients the TIFF
// grayscale path uses. Converting to RGB only retags: appearance pixels
// already ARE the RGB rendering of whatever the document held.
//
// Colour-bearing items converted: raster layer buffers (fresh Images,
// copy-on-write for the undo snapshot), live-text colours (value copies),
// and the canvas paper. Vector `art` is deliberately untouched — it is a
// shared immutable SVG source (see the field comment) and its colours
// separate when the composite flattens for export anyway.
// Foreground/background swatches are UI state outside the snapshot, so
// converting them would desync undo.
//
// One undo step: DocumentSnapshot carries `colorMode`/`iccProfile`, so
// the tag follows the pixels back and forth.

#include "ui/color_mode.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <memory>

#include "engine/color/icc_convert.h"
#include "engine/core/image.h"
#include "ui/app_state.h"
#include "ui/settings.h"

namespace pittore::ui {
namespace {

// Common system installs, probed when neither the document nor Settings.toml
// names a destination. Read at runtime, never bundled.
const char* const kSystemCmyk[] = {
    "/usr/share/ghostscript/iccprofiles/default_cmyk.icc",
    "/usr/share/color/icc/colord/ISOcoated_v2_eci.icc",
    "/usr/share/color/icc/colord/USWebCoatedSWOP.icc",
    "/usr/share/color/icc/colord/UncoatedFOGRA39.icc",
};

QByteArray readFileBytes(const QString& path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray b = f.readAll();
    if (b.isEmpty() || b.size() > (1 << 24)) return {};
    return b;
}

// Usable = LCMS2 opens it as a CMYK profile whose encoder self-test passes.
// Without LCMS2 this is always false, so minimal builds resolve to the
// naive fallback through the same path as everyone else.
bool usableCmyk(const QByteArray& bytes) {
    if (bytes.isEmpty()) return false;
    const pittore::color::SrgbToCmyk probe(
        reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()));
    return probe.valid();
}

}  // namespace

QByteArray resolveCmykProfile(const DocumentItem& doc) {
    // 1. The document's own separation (imported CMYK source, or a previous
    //    conversion): exports and re-conversions stay on that profile.
    if (usableCmyk(doc.iccProfile)) return doc.iccProfile;
    // 2. The configured destination.
    AppSettings s;
    if (loadSettings(s)) {
        const QByteArray cfg = readFileBytes(s.cmykProfile);
        if (usableCmyk(cfg)) return cfg;
    }
    // 3. Common system installs.
    for (const char* p : kSystemCmyk) {
        const QByteArray sys = readFileBytes(QString::fromUtf8(p));
        if (usableCmyk(sys)) return sys;
    }
    // 4. Nothing: callers separate naively (and the UI says so).
    return {};
}

QString defaultCmykProfilePath() {
    AppSettings s;
    if (loadSettings(s) && !s.cmykProfile.isEmpty() &&
        QFileInfo::exists(s.cmykProfile))
        return s.cmykProfile;
    for (const char* p : kSystemCmyk) {
        const QString candidate = QString::fromUtf8(p);
        if (QFileInfo::exists(candidate)) return candidate;
    }
    return {};
}

bool AppState::convertDocumentMode(ModeTarget target) {
    DocumentItem* d = activeDocument();
    if (!d || d->size.isEmpty()) return false;

    // Mode changes storage, not depth: keep the document's bit-depth suffix
    // verbatim for RGB and Grayscale (the float buffer doesn't care; the
    // writers read it back). CMYK has no 32-bit form in our tags and the
    // only depths it shares with us are 8/16, so /16 stays and everything
    // else lands on /8 — which is also the export depth /32 already had.
    const QString suffix =
        d->colorMode.contains(QLatin1Char('/'))
            ? d->colorMode.mid(d->colorMode.indexOf(QLatin1Char('/')))
            : QStringLiteral("/8");
    const QString tag =
        (target == ModeTarget::Rgb         ? QStringLiteral("RGB")
         : target == ModeTarget::Grayscale ? QStringLiteral("Grayscale")
                                           : QStringLiteral("CMYK")) +
        (target == ModeTarget::Cmyk && suffix != QLatin1String("/16")
             ? QStringLiteral("/8")
             : suffix);
    // RGB -> RGB and Gray -> Gray would be a no-op. CMYK always runs: a
    // re-conversion through a different destination genuinely re-separates.
    if (target != ModeTarget::Cmyk && d->colorMode == tag) return false;

    const bool toCmyk = target == ModeTarget::Cmyk;
    const bool toGray = target == ModeTarget::Grayscale;

    QByteArray icc;
    std::unique_ptr<pittore::color::SrgbToCmyk> fwd;
    std::unique_ptr<pittore::color::CmykToSrgb> back;
    if (toCmyk) {
        icc = resolveCmykProfile(*d);
        fwd = std::make_unique<pittore::color::SrgbToCmyk>(
            reinterpret_cast<const std::uint8_t*>(icc.constData()),
            static_cast<std::size_t>(icc.size()));
        back = std::make_unique<pittore::color::CmykToSrgb>(
            reinterpret_cast<const std::uint8_t*>(icc.constData()),
            static_cast<std::size_t>(icc.size()));
    }

    // Appearance -> target -> appearance. Gray writes Rec.709 luma; CMYK
    // round-trips the separation; RGB here is the identity (retag only —
    // the pixel pass still copies through, so a CMYK -> RGB undo step has
    // something real to restore).
    auto convRgb = [&](float& r, float& g, float& b) {
        if (toGray) {
            const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            r = g = b = std::clamp(l, 0.0f, 1.0f);
        } else if (toCmyk) {
            float ink[4];
            fwd->convert(r, g, b, ink);
            float out[3];
            back->convert(ink[0], ink[1], ink[2], ink[3], out);
            r = std::clamp(out[0], 0.0f, 1.0f);
            g = std::clamp(out[1], 0.0f, 1.0f);
            b = std::clamp(out[2], 0.0f, 1.0f);
        }
    };
    auto convColor = [&](QColor c) -> QColor {
        if (!c.isValid()) return c;
        float r = static_cast<float>(c.redF());
        float g = static_cast<float>(c.greenF());
        float b = static_cast<float>(c.blueF());
        convRgb(r, g, b);
        return QColor::fromRgbF(r, g, b, c.alphaF());
    };

    beginUndoStep();
    for (LayerItem& layer : d->layers) {
        // Live text carries its own colours (value copies: the snapshot has
        // its own). Text layers are Kind::Pixel with an `isText` flag; some
        // imports use Kind::Text itself, so test both. Shape/vector paint
        // lives in the shared immutable `art`.
        if ((layer.isText || layer.kind == LayerItem::Kind::Text) &&
            (toCmyk || toGray)) {
            layer.textSpec.color = convColor(layer.textSpec.color);
            layer.textSpec.underlineColor =
                convColor(layer.textSpec.underlineColor);
            layer.textSpec.strikeColor = convColor(layer.textSpec.strikeColor);
            layer.textSpec.backgroundColor =
                convColor(layer.textSpec.backgroundColor);
        }
        if (layer.kind == LayerItem::Kind::Group) continue;
        if (!layer.pixels || layer.pixels->width() == 0 ||
            layer.pixels->height() == 0)
            continue;
        // Fresh buffer: the pre-action snapshot keeps the old shared Image.
        auto dst = std::make_shared<pittore::Image>(layer.pixels->width(),
                                                     layer.pixels->height());
        const pittore::RGBAf* src = layer.pixels->data();
        pittore::RGBAf* out = dst->data();
        const std::size_t n = layer.pixels->pixel_count();
        for (std::size_t i = 0; i < n; ++i) {
            pittore::RGBAf px = src[i];
            convRgb(px.r, px.g, px.b);
            out[i] = px;
        }
        layer.pixels = dst;
        ++layer.sourceStamp;
        layer.thumbnail = QImage();
    }
    if (toCmyk || toGray) d->canvasPaper = convColor(d->canvasPaper);

    // The separation bytes are the document's own: keep them on CMYK so
    // exports round-trip through the same profile, drop them elsewhere so a
    // stale CMYK profile never rides an RGB/Gray document.
    if (toCmyk)
        d->iccProfile = icc;
    else
        d->iccProfile.clear();
    d->colorMode = tag;
    d->rebuildComposite();
    commitUndoStep(target == ModeTarget::Cmyk       ? tr("Convert to CMYK")
                   : target == ModeTarget::Grayscale ? tr("Convert to Grayscale")
                                                     : tr("Convert to RGB"),
                   QStringLiteral("color"));
    return true;
}

}  // namespace pittore::ui
