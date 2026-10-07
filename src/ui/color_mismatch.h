#pragma once
#include <QString>

class QWidget;

namespace pittore::ui {

// How an imported file's embedded color profile resolves against the
// working space (Settings > Documents). Mirrors the conventional "Embedded
// Profile Mismatch" options plus Cancel (abort the open).
enum class ImportProfileChoice {
    UseEmbedded,      // keep pixels, tag the document with the embedded profile
    ConvertToWorking,  // tag the document with the working profile (pixels are
                       // transformed when the pipeline can: non-RGB sources via
                       // the naive core at decode; same-model retags until the
                       // CMS policy in features.md #7 lands)
    Discard,           // strip the tag (untagged document)
    Cancel,            // abort the open entirely
};

// Mismatch policy values (Settings > Documents > On profile mismatch):
// 0 = ask on mismatch, 1 = convert silently, 2 = keep embedded silently,
// 3 = always ask about any embedded profile (conventional behaviour: it compares
// profile bytes, so even a byte-different sRGB prompts there).
enum class ProfileMismatchPolicy { Ask = 0, Convert = 1, Keep = 2, AlwaysAsk = 3 };

// Modal mismatch dialog (Cancel/closed = Cancel). `useEmbedded` is
// pre-selected, matching conventional behaviour.
ImportProfileChoice askImportedProfile(QWidget* parent,
                                       const QString& embedded,
                                       const QString& working);

}  // namespace pittore::ui
