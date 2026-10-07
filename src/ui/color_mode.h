#pragma once
// Image > Mode (RGB / Grayscale / CMYK): the destination-profile resolver
// shared by the mode conversion (color_mode.cpp), the layered-PSD writer
// (psd_export.cpp) and the TIFF export path (export_writers.cpp). The
// conversion itself hangs off AppState — declared in ui/app_state.h next
// to transformDocumentImage.

#include <QByteArray>
#include <QString>

namespace pittore::ui {

struct DocumentItem;

// Destination CMYK profile bytes for `doc`, in resolution order: the
// document's own separation (imported CMYK source or an earlier
// conversion), then the configured path in Settings.toml, then a few
// common system installs (read at runtime, never bundled), then empty —
// which every caller treats as "naive full-GCR, not colour-managed"
// rather than an error. Profiled consumers get a live LCMS2 transform,
// everyone else falls back inside the converter; nothing branches on the
// result being present.
QByteArray resolveCmykProfile(const DocumentItem& doc);

// Best profile PATH to prefill a picker with: the configured destination if
// it exists, else the first existing system install, else empty. (Unlike the
// resolver above this never returns the document's embedded bytes — those
// have no path to show.)
QString defaultCmykProfilePath();

}  // namespace pittore::ui
