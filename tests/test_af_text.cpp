// test_af_text.cpp — end-to-end check of the text-layer decode: read the
// rotated-text probe, composite the recovered layers over white and compare
// against the file's own embedded thumbnail (Affinity's render of the same
// document). The bound matches the tolerance the importer targets, which is
// looser than the shapes' because the raster is resampled through the rotation.
#include <cstdio>
#include <string>

#include "af_probe.h"
#include "test_util.h"

namespace {

using pittore::probe::decodeThumb;
using pittore::probe::probeDir;
using pittore::probe::readFile;
using pittore::probe::thumbRms;

void run() {
    const std::string dir = probeDir();
    if (dir.empty()) return;
    auto bytes = readFile(dir + "/text_rotated.af");
    if (!bytes) {
        std::printf("  skipping: no text_rotated.af\n");
        return;
    }
    auto doc = pittore::io::afDecodeLayers(*bytes, nullptr, dir);
    CHECK(doc.has_value());
    if (!doc) return;
    for (const std::string& line : doc->log) std::printf("    %s\n", line.c_str());
    auto thumb = decodeThumb(*bytes);
    CHECK(thumb.has_value());
    if (!thumb) return;

    // A text layer must actually come back as pixels, not a skip.
    bool havePixels = false;
    for (const pittore::io::AfLayer& l : doc->layers)
        if (!l.isGroup && !l.rgba.empty()) havePixels = true;
    CHECK(havePixels);

    const double rms = thumbRms(*doc, *thumb);
    std::printf("  text_rotated.af %ux%u rms %.2f (bound 2.5)\n", doc->width, doc->height, rms);
    CHECK(rms <= 2.5);
}

}  // namespace

TEST_MAIN_CALL(run)
