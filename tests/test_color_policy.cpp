// test_color_policy.cpp — import profile policy: canonical comparison,
// resolveImportedProfile across policies + resolver, applyImportedProfile
// tagging and the Cancel contract, and the mismatch dialog default.
//
// Runs headless (QT_QPA_PLATFORM=offscreen, scratch XDG_CONFIG_HOME).
#include <QApplication>
#include <QColorSpace>
#include <QDialog>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QFile>
#include <QFontMetrics>
#include <QLabel>
#include <QRadioButton>
#include <QTimer>

#include <string>
#include <vector>

#include <zlib.h>

#include "engine/io/icc.h"
#include "engine/io/icc_scan.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/color_mismatch.h"

using namespace pittore::ui;

namespace {

void test_canonical() {
    CHECK(canonicalProfileName(QStringLiteral("sRGB")) ==
          canonicalProfileName(QStringLiteral("sRGB IEC61966-2.1")));
    CHECK(canonicalProfileName(QStringLiteral("Srgb Iec61966-2.1")) ==
          canonicalProfileName(QStringLiteral("sRGB IEC61966-2.1")));
    CHECK(canonicalProfileName(QStringLiteral("Adobe RGB (1998)")) ==
          canonicalProfileName(QStringLiteral("Adobe RGB")));
    CHECK(canonicalProfileName(QStringLiteral("Display P3")) ==
          canonicalProfileName(QStringLiteral("Display-P3")));
    CHECK(canonicalProfileName(QStringLiteral("U.S. Web Coated (SWOP) v2")) !=
          canonicalProfileName(QStringLiteral("GRACoL2006_Coated1v2")));
    CHECK(canonicalProfileName(QString()).isEmpty());
}

void test_resolve() {
    AppState state;  // scratch settings: working sRGB, no resolver
    const QString working = state.settings().workingProfile;
    CHECK(working == QStringLiteral("sRGB IEC61966-2.1"));
    CHECK(state.settings().colorMismatchPolicy == 3);  // AlwaysAsk default
    // The fresh default is AlwaysAsk; pin Ask explicitly for the
    // classic-behavior assertions below.
    {
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 0;
        state.applySettings(next);
    }

    // Missing profile: silently assume the working space.
    auto missing = state.resolveImportedProfile(QString());
    CHECK(missing && *missing == working);
    // Family match: embedded kept, no prompt, no resolver needed.
    auto same = state.resolveImportedProfile(QStringLiteral("sRGB"));
    CHECK(same && *same == QStringLiteral("sRGB"));
    // Mismatch with no resolver: Ask falls back to Convert.
    auto fallback =
        state.resolveImportedProfile(QStringLiteral("Adobe RGB (1998)"));
    CHECK(fallback && *fallback == working);

    // Keep policy.
    {
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 2;
        state.applySettings(next);
    }
    auto kept = state.resolveImportedProfile(QStringLiteral("Adobe RGB (1998)"));
    CHECK(kept && *kept == QStringLiteral("Adobe RGB (1998)"));

    // Convert policy.
    {
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 1;
        state.applySettings(next);
    }
    auto converted =
        state.resolveImportedProfile(QStringLiteral("Adobe RGB (1998)"));
    CHECK(converted && *converted == working);

    // Ask + resolver: every choice maps through, Cancel is sticky-null.
    {
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 0;
        state.applySettings(next);
    }
    state.setProfileMismatchResolver(
        [](const QString&, const QString&) {
            return ImportProfileChoice::Discard;
        });
    auto discarded =
        state.resolveImportedProfile(QStringLiteral("Adobe RGB (1998)"));
    CHECK(discarded && discarded->isEmpty());
    state.setProfileMismatchResolver(
        [](const QString&, const QString&) {
            return ImportProfileChoice::Cancel;
        });
    CHECK(!state.resolveImportedProfile(QStringLiteral("Adobe RGB (1998)")));
    state.setProfileMismatchResolver({});
    auto again =
        state.resolveImportedProfile(QStringLiteral("Adobe RGB (1998)"));
    CHECK(again && *again == working);

    // AlwaysAsk: even a family match prompts.
    {
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 3;
        state.applySettings(next);
    }
    bool asked = false;
    state.setProfileMismatchResolver(
        [&](const QString& embedded, const QString&) {
            asked = true;
            CHECK(embedded == QStringLiteral("sRGB"));
            return ImportProfileChoice::UseEmbedded;
        });
    auto matched =
        state.resolveImportedProfile(QStringLiteral("sRGB"));
    CHECK(asked);
    CHECK(matched && *matched == QStringLiteral("sRGB"));
    state.setProfileMismatchResolver({});
}

void test_apply() {
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(16, 16), 72);
    CHECK(doc != nullptr);
    if (!doc) return;
    state.setProfileMismatchResolver(
        [](const QString&, const QString&) {
            return ImportProfileChoice::UseEmbedded;
        });
    CHECK(state.applyImportedProfile(*doc, QStringLiteral("Adobe RGB (1998)")));
    CHECK(doc->profile == QStringLiteral("Adobe RGB (1998)"));
    // Cancel leaves the tag (and the document) alone.
    state.setProfileMismatchResolver(
        [](const QString&, const QString&) {
            return ImportProfileChoice::Cancel;
        });
    CHECK(!state.applyImportedProfile(*doc, QStringLiteral("ProPhoto RGB")));
    CHECK(doc->profile == QStringLiteral("Adobe RGB (1998)"));
    state.closeDocument(0);
}

// Minimal spec-shaped ICC profile: 128-byte header plus a 'desc' tag naming
// `profile`. Shared by the PSD fixture below and the container fixtures, so
// every path in this file resolves the same readable description.
std::vector<std::uint8_t> buildIccBlob(const std::string& profile) {
    std::vector<std::uint8_t> icc(128, 0);
    auto icc32 = [&](std::uint32_t v) {
        icc.push_back(static_cast<std::uint8_t>(v >> 24));
        icc.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
        icc.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        icc.push_back(static_cast<std::uint8_t>(v & 0xff));
    };
    icc32(1);
    icc.insert(icc.end(), {'d', 'e', 's', 'c'});
    icc32(144);
    icc32(static_cast<std::uint32_t>(12 + profile.size() + 1));
    icc.insert(icc.end(), {'d', 'e', 's', 'c'});
    icc32(0);
    icc32(static_cast<std::uint32_t>(profile.size()) + 1);
    icc.insert(icc.end(), profile.begin(), profile.end());
    icc.push_back(0);
    return icc;
}

// Minimal spec-shaped CMYK PSD (2x1, RAW, 4 channels, stored inverted) with
// a 0x0422 ICC block, to prove the mismatch workflow fires end to end.
std::vector<std::uint8_t> buildCmykPsdWithIcc(const std::string& profile) {
    std::vector<std::uint8_t> out;
    auto put16 = [&](std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v >> 8));
        out.push_back(static_cast<std::uint8_t>(v & 0xff));
    };
    auto put32 = [&](std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v >> 24));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        out.push_back(static_cast<std::uint8_t>(v & 0xff));
    };
    out.insert(out.end(), {'8', 'B', 'P', 'S'});
    put16(1);
    out.insert(out.end(), 6, 0);
    put16(4);       // C,M,Y,K
    put32(1);       // height
    put32(2);       // width
    put16(8);       // depth
    put16(4);       // CMYK
    put32(0);       // color mode data
    // Image resources: one 0x0422 block with a 'desc' ICC profile.
    const std::vector<std::uint8_t> icc = buildIccBlob(profile);
    std::vector<std::uint8_t> res;
    res.insert(res.end(), {'8', 'B', 'I', 'M'});
    res.push_back(0x04);
    res.push_back(0x22);
    res.push_back(0);
    res.push_back(0);  // empty Pascal name
    const std::uint32_t iccLen = static_cast<std::uint32_t>(icc.size());
    res.push_back(static_cast<std::uint8_t>(iccLen >> 24));
    res.push_back(static_cast<std::uint8_t>((iccLen >> 16) & 0xff));
    res.push_back(static_cast<std::uint8_t>((iccLen >> 8) & 0xff));
    res.push_back(static_cast<std::uint8_t>(iccLen & 0xff));
    res.insert(res.end(), icc.begin(), icc.end());
    if (icc.size() & 1) res.push_back(0);
    put32(static_cast<std::uint32_t>(res.size()));
    out.insert(out.end(), res.begin(), res.end());
    put32(0);  // layer + mask info
    put16(0);  // compression: raw
    // Paper-white planes (stored 255 = no ink), 2 px each, already even.
    for (int ch = 0; ch < 4; ++ch) {
        out.push_back(255);
        out.push_back(255);
    }
    return out;
}

void test_cmyk_mismatch_end_to_end() {
    // A CMYK file carrying an ICC profile reaches the resolver with the
    // embedded name, and the choice lands on the document tag.
    // embedded name, and the choice lands on the document tag.
    const QString path =
        QDir::tempPath() + QStringLiteral("/pittore-cmyk-mismatch.psd");
    {
        const std::vector<std::uint8_t> psd =
            buildCmykPsdWithIcc("Fake CMYK Test");
        QFile f(path);
        CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        if (f.isOpen()) {
            f.write(reinterpret_cast<const char*>(psd.data()),
                    static_cast<qint64>(psd.size()));
        }
    }
    AppState state;
    QString seenEmbedded;
    QString seenWorking;
    state.setProfileMismatchResolver(
        [&](const QString& embedded, const QString& working) {
            seenEmbedded = embedded;
            seenWorking = working;
            return ImportProfileChoice::UseEmbedded;
        });
    QString error;
    CHECK(state.openImageFile(path, &error));
    CHECK(seenEmbedded == QStringLiteral("Fake CMYK Test"));
    CHECK(seenWorking == state.settings().workingProfile);
    CHECK(state.activeDocument() != nullptr);
    if (state.activeDocument())
        CHECK(state.activeDocument()->profile ==
              QStringLiteral("Fake CMYK Test"));
    state.closeDocument(0);
    QFile::remove(path);
}

// --- Container fixtures: JPEG (APP2) and PNG (iCCP) ------------------------

void putPngChunk(std::vector<std::uint8_t>& out, const char* type,
                 const std::vector<std::uint8_t>& data) {
    const std::uint32_t len = static_cast<std::uint32_t>(data.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(len >> shift));
    const std::size_t typeAt = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + typeAt, 4 + data.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(crc >> shift));
}

std::vector<std::uint8_t> zlibWrap(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z(compressBound(raw.size()) + 64);
    uLongf zlen = z.size();
    if (compress2(z.data(), &zlen, raw.data(), static_cast<uLong>(raw.size()),
                  Z_BEST_SPEED) != Z_OK)
        return {};
    z.resize(zlen);
    return z;
}

// JPEG carrying `icc` split over two APP2 segments — real writers do exactly
// that once a profile outgrows one segment's payload.
std::vector<std::uint8_t> buildJpegWithIcc(
    const std::vector<std::uint8_t>& icc) {
    std::vector<std::uint8_t> out = {0xFF, 0xD8};  // SOI
    const std::size_t half = (icc.size() + 1) / 2;
    for (int part = 0; part < 2; ++part) {
        std::vector<std::uint8_t> seg;
        seg.insert(seg.end(),
                   {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0});
        seg.push_back(static_cast<std::uint8_t>(part + 1));
        seg.push_back(2);
        const std::size_t from = part == 0 ? 0 : half;
        const std::size_t to = part == 0 ? half : icc.size();
        seg.insert(seg.end(), icc.begin() + from, icc.begin() + to);
        out.push_back(0xFF);
        out.push_back(0xE2);  // APP2
        const std::uint16_t len = static_cast<std::uint16_t>(seg.size() + 2);
        out.push_back(static_cast<std::uint8_t>(len >> 8));
        out.push_back(static_cast<std::uint8_t>(len & 0xff));
        out.insert(out.end(), seg.begin(), seg.end());
    }
    out.push_back(0xFF);
    out.push_back(0xD9);  // EOI: the scan stops here
    return out;
}

// PNG carrying `icc` in its iCCP chunk; withPixels adds a decodable 2×1 RGB
// IDAT so the file opens as an image as well.
std::vector<std::uint8_t> buildPngWithIcc(const std::vector<std::uint8_t>& icc,
                                          bool withPixels) {
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G',
                                     0x0D, 0x0A, 0x1A, 0x0A};
    putPngChunk(out, "IHDR",
                {0, 0, 0, 2, 0, 0, 0, 1, 8, 2, 0, 0, 0});  // 2×1, RGB8
    std::vector<std::uint8_t> iccp = {'i', 'c', 'c', 0, 0};
    const std::vector<std::uint8_t> packed = zlibWrap(icc);
    iccp.insert(iccp.end(), packed.begin(), packed.end());
    putPngChunk(out, "iCCP", iccp);
    if (withPixels) {
        const std::vector<std::uint8_t> raw = {0, 255, 0, 0, 0, 0, 255};
        putPngChunk(out, "IDAT", zlibWrap(raw));
    }
    putPngChunk(out, "IEND", {});
    return out;
}

void test_container_icc_scan() {
    // The toolkit reports no color space for a profile it cannot model, so
    // the import policy reads the profile out of the container instead: this
    // is what keeps an embedded profile from reading as "nothing embedded"
    // (and the mismatch dialog from never firing).
    const std::string name = "Fake CMYK Test";
    const std::vector<std::uint8_t> icc = buildIccBlob(name);
    std::vector<std::uint8_t> got;
    const auto jpg = buildJpegWithIcc(icc);
    CHECK(pittore::io::containerIccProfile(jpg.data(), jpg.size(), &got));
    CHECK(got == icc);
    CHECK(QString::fromStdString(pittore::io::iccProfileDescription(
              got.data(), got.size())) == QString::fromStdString(name));
    const auto png = buildPngWithIcc(icc, false);
    got.clear();
    CHECK(pittore::io::containerIccProfile(png.data(), png.size(), &got));
    CHECK(got == icc);
    // Nothing embedded says so; a truncated or foreign buffer never reads
    // past its end.
    const std::vector<std::uint8_t> bare = {0xFF, 0xD8, 0xFF, 0xD9};
    CHECK(!pittore::io::containerIccProfile(bare.data(), bare.size(), &got));
    CHECK(!pittore::io::containerIccProfile(png.data(), 16, &got));
    const std::vector<std::uint8_t> junk(64, 0x5A);
    CHECK(!pittore::io::containerIccProfile(junk.data(), junk.size(), &got));
}

void test_png_mismatch_end_to_end() {
    // Same end-to-end contract as the PSD fixture, but through a format the
    // decode declines: the image's color space comes back empty, so only the
    // container scan can hand the policy the embedded name.
    const QString path =
        QDir::tempPath() + QStringLiteral("/pittore-container-icc.png");
    const std::vector<std::uint8_t> png =
        buildPngWithIcc(buildIccBlob("Fake CMYK Test"), true);
    {
        QFile f(path);
        CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        if (f.isOpen())
            f.write(reinterpret_cast<const char*>(png.data()),
                    static_cast<qint64>(png.size()));
    }
    // The decode must genuinely report nothing, or this asserts nothing.
    const QImage probe(path);
    CHECK(!probe.isNull());
    CHECK(probe.colorSpace().description().isEmpty());
    AppState state;
    {
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 3;  // AlwaysAsk
        state.applySettings(next);
    }
    bool asked = false;
    QString seen;
    state.setProfileMismatchResolver(
        [&](const QString& embedded, const QString&) {
            asked = true;
            seen = embedded;
            return ImportProfileChoice::UseEmbedded;
        });
    QString error;
    CHECK(state.openImageFile(path, &error));
    CHECK(asked);
    CHECK(seen == QStringLiteral("Fake CMYK Test"));
    if (state.activeDocument())
        CHECK(state.activeDocument()->profile ==
              QStringLiteral("Fake CMYK Test"));
    if (!state.documents().isEmpty()) state.closeDocument(0);
    QFile::remove(path);
}

void test_deferred() {
    // With deferral on, Ask stores the question and tags
    // provisionally instead of prompting; the drain asks over the painted
    // UI, applies the choice, and rolls back on Cancel.
    AppState state;
    {
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 0;
        state.applySettings(next);
    }
    DocumentItem* doc = state.addDocument(QStringLiteral("t"), QSize(16, 16), 72);
    CHECK(doc != nullptr);
    if (!doc) return;
    bool asked = false;
    state.setProfileMismatchResolver(
        [&](const QString&, const QString&) {
            asked = true;
            return ImportProfileChoice::UseEmbedded;
        });
    state.setDeferMismatchDialogs(true);
    CHECK(state.applyImportedProfile(*doc, QStringLiteral("Adobe RGB (1998)")));
    CHECK(!asked);
    CHECK(state.hasPendingProfileMismatch());
    CHECK(doc->profile == state.settings().workingProfile);  // provisional
    CHECK(state.resolvePendingProfileMismatch());
    CHECK(asked);
    CHECK(!state.hasPendingProfileMismatch());
    CHECK(doc->profile == QStringLiteral("Adobe RGB (1998)"));
    // Cancel rolls the document back.
    state.setDeferMismatchDialogs(true);
    state.setProfileMismatchResolver(
        [](const QString&, const QString&) {
            return ImportProfileChoice::Cancel;
        });
    CHECK(state.applyImportedProfile(*doc, QStringLiteral("ProPhoto RGB")));
    CHECK(state.resolvePendingProfileMismatch() == false);
    CHECK(state.documents().isEmpty());
    // Draining with nothing pending is a no-op success.
    CHECK(state.resolvePendingProfileMismatch());
}

void test_bing_jpeg_end_to_end() {
    // A real-world sRGB JPEG with an embedded ICC profile: with AlwaysAsk the
    // resolver must fire even though the family matches the working space.
    // Skips unless PITTORE_PROFILE_JPEG_FIXTURE points at such a file.
    const QString src =
        QString::fromLocal8Bit(qgetenv("PITTORE_PROFILE_JPEG_FIXTURE"));
    if (src.isEmpty() || !QFileInfo::exists(src)) {
        std::printf("  (skip: PITTORE_PROFILE_JPEG_FIXTURE not set)\n");
        return;
    }
    AppState state;
    {
        AppSettings next = state.settings();
        next.colorMismatchPolicy = 3;
        state.applySettings(next);
    }
    bool asked = false;
    QString seenEmbedded;
    state.setProfileMismatchResolver(
        [&](const QString& embedded, const QString&) {
            asked = true;
            seenEmbedded = embedded;
            return ImportProfileChoice::UseEmbedded;
        });
    QString error;
    CHECK(state.openImageFile(src, &error));
    CHECK(asked);
    CHECK(seenEmbedded == QStringLiteral("sRGB IEC61966-2.1"));
    CHECK(state.activeDocument() != nullptr);
    if (state.activeDocument())
        CHECK(state.activeDocument()->profile ==
              QStringLiteral("sRGB IEC61966-2.1"));
    state.closeDocument(0);
}

void test_proof() {
    AppState state;
    CHECK(!state.proofEnabled());
    CHECK(!state.proofGamut());
    int proofSignals = 0;
    QObject::connect(&state, &AppState::proofChanged, &state,
                     [&] { ++proofSignals; });
    state.setProofEnabled(true);
    CHECK(state.proofEnabled());
    state.setProofEnabled(true);  // no-op: no second emission
    state.setProofGamut(true);
    CHECK(state.proofGamut());
    CHECK(proofSignals == 2);
    // Setup persists through applySettings and re-emits for the canvas.
    // (Reset first: the scratch config persists across runs, so the change
    // must be relative, not absolute.)
    {
        AppSettings reset = state.settings();
        reset.proofProfile.clear();
        reset.proofIntent = 1;
        reset.proofBpc = true;
        state.applySettings(reset);
    }
    const int beforeSetup = proofSignals;
    AppSettings next = state.settings();
    next.proofProfile = QStringLiteral("/tmp/coated.icc");
    next.proofIntent = 0;
    next.proofBpc = false;
    state.applySettings(next);
    CHECK(state.settings().proofProfile ==
          QStringLiteral("/tmp/coated.icc"));
    CHECK(state.settings().proofIntent == 0);
    CHECK(!state.settings().proofBpc);
    CHECK(proofSignals == beforeSetup + 1);
    // Toggles are session-only: a fresh state starts unproofed even though
    // the setup round-trips through Settings.toml.
    AppState fresh;
    CHECK(!fresh.proofEnabled());
    CHECK(!fresh.proofGamut());
    CHECK(fresh.settings().proofProfile ==
          QStringLiteral("/tmp/coated.icc"));
}

void test_dialog() {
    bool sawNames = false;
    bool defaultUse = false;
    bool roomy = false;
    QTimer::singleShot(0, [&] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg || !dlg->isVisible() ||
                dlg->windowTitle() !=
                    QStringLiteral("Embedded Profile Mismatch"))
                continue;
            for (QLabel* l : dlg->findChildren<QLabel*>()) {
                if (l->text().contains(QStringLiteral("Adobe RGB (1998)")) &&
                    l->text().contains(QStringLiteral("sRGB IEC61966-2.1")))
                    sawNames = true;
            }
            const auto radios = dlg->findChildren<QRadioButton*>();
            if (!radios.isEmpty() && radios.first()->isChecked())
                defaultUse = true;
            // Regression: the dialog must size to its content, not squash.
            dlg->adjustSize();
            if (dlg->minimumWidth() >= 400 && dlg->sizeHint().height() >=
                    radios.size() * QFontMetrics(dlg->font()).height())
                roomy = true;
            dlg->accept();
        }
    });
    CHECK(askImportedProfile(nullptr, QStringLiteral("Adobe RGB (1998)"),
                             QStringLiteral("sRGB IEC61966-2.1")) ==
          ImportProfileChoice::UseEmbedded);
    CHECK(sawNames);
    CHECK(defaultUse);
    CHECK(roomy);

    // Matching families get honest text instead of a mismatch claim.
    bool sawSameMeaning = false;
    QTimer::singleShot(0, [&] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg || !dlg->isVisible() ||
                dlg->windowTitle() !=
                    QStringLiteral("Embedded Profile Mismatch"))
                continue;
            for (QLabel* l : dlg->findChildren<QLabel*>()) {
                if (l->text().contains(QStringLiteral("same meaning")))
                    sawSameMeaning = true;
            }
            dlg->accept();
        }
    });
    CHECK(askImportedProfile(nullptr, QStringLiteral("sRGB"),
                             QStringLiteral("sRGB IEC61966-2.1")) ==
          ImportProfileChoice::UseEmbedded);
    CHECK(sawSameMeaning);

    QTimer::singleShot(0, [] {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (dlg && dlg->isVisible()) dlg->reject();
        }
    });
    CHECK(askImportedProfile(nullptr, QStringLiteral("X"),
                             QStringLiteral("Y")) ==
          ImportProfileChoice::Cancel);
}

}  // namespace

int main(int argc, char** argv) {
    // Never touch the user's real config on manual runs: meson already
    // points XDG_CONFIG_HOME at a scratch dir (see tests/meson.build), so
    // only redirect when it is unset.
    if (qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME")) {
        static QTemporaryDir* scratch = new QTemporaryDir;
        if (scratch->isValid())
            qputenv("XDG_CONFIG_HOME", scratch->path().toLocal8Bit());
    }
    // The scratch dir persists between runs, and every AppState below saves
    // the settings it applied — including the policies later assertions call
    // "the fresh default". Drop them so each run starts from factory state.
    // Drop the legacy path too so a pre-rename artifact can never migrate in.
    QFile::remove(QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
                      .filePath(QStringLiteral("PittoreStudio/Settings.toml")));
    QFile::remove(QDir(qEnvironmentVariable("XDG_CONFIG_HOME"))
                      .filePath(QStringLiteral("InfinityPhoto/Settings.toml")));
    QApplication app(argc, argv);
    test_canonical();
    test_resolve();
    test_apply();
    test_proof();
    test_deferred();
    test_cmyk_mismatch_end_to_end();
    test_container_icc_scan();
    test_png_mismatch_end_to_end();
    test_bing_jpeg_end_to_end();
    test_dialog();
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
