// XCF flattened-composite decoder/encoder: RLE round-trip, a hand-built
// RAW-compressed file, and error handling.
#include "engine/io/xcf.h"

#include <cstdio>
#include <string>
#include <vector>

using pittore::io::XcfImage;
using pittore::io::xcfDecode;
using pittore::io::xcfEncodeRgba;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (cond) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

void put32be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
}

// A minimal RAW-compressed RGBA XCF (2x2), single layer, no projections.
std::vector<std::uint8_t> buildRawXcf() {
    const std::uint8_t r[4] = {1, 2, 3, 4};
    const std::uint8_t g[4] = {5, 6, 7, 8};
    const std::uint8_t b[4] = {9, 10, 11, 12};
    const std::uint8_t a[4] = {255, 255, 255, 255};
    std::vector<std::uint8_t> out;
    const char* magic = "gimp xcf ";
    out.insert(out.end(), magic, magic + 9);
    out.insert(out.end(), {'v', '1', '0', '0'});
    out.insert(out.end(), 3, 0);
    put32be(out, 2);  // width
    put32be(out, 2);  // height
    put32be(out, 0);  // RGB base type
    put32be(out, 0);  // END image properties
    put32be(out, 1);  // one layer
    const char* name = "Layer 1";
    put32be(out, 8);  // name length incl. NUL
    out.insert(out.end(), name, name + 7);
    out.push_back('\0');
    put32be(out, 2);  // layer width
    put32be(out, 2);  // layer height
    put32be(out, 1);  // RGBA layer
    put32be(out, 17);  // compression property
    put32be(out, 4);
    put32be(out, 0);   // RAW
    put32be(out, 0);   // END properties
    put32be(out, 0);   // mask: empty name
    put32be(out, 0);   // mask offset
    const std::size_t offsetPos = out.size();
    put32be(out, 0);   // data offset (patched)
    put32be(out, 0);   // no channels
    put32be(out, 0);   // no projections
    const std::uint32_t dataOffset = static_cast<std::uint32_t>(out.size());
    out.insert(out.end(), r, r + 4);
    out.insert(out.end(), g, g + 4);
    out.insert(out.end(), b, b + 4);
    out.insert(out.end(), a, a + 4);
    out[offsetPos + 0] = static_cast<std::uint8_t>(dataOffset >> 24);
    out[offsetPos + 1] = static_cast<std::uint8_t>((dataOffset >> 16) & 0xff);
    out[offsetPos + 2] = static_cast<std::uint8_t>((dataOffset >> 8) & 0xff);
    out[offsetPos + 3] = static_cast<std::uint8_t>(dataOffset & 0xff);
    return out;
}

}  // namespace

int main() {
    // --- RLE round-trip (8-bit scale) --------------------------------------
    const std::uint32_t w = 5, h = 3;
    std::vector<std::uint16_t> rgba(w * h * 4);
    for (std::uint32_t i = 0; i < w * h; ++i) {
        const std::uint16_t v = static_cast<std::uint16_t>((i % 251) * 257);
        rgba[i * 4 + 0] = v;
        rgba[i * 4 + 1] = static_cast<std::uint16_t>(((i * 3) % 256) * 257);
        rgba[i * 4 + 2] = static_cast<std::uint16_t>(((i * 7) % 256) * 257);
        rgba[i * 4 + 3] = (i == 1) ? 0u : 65535u;
    }
    auto xcf = xcfEncodeRgba(w, h, rgba.data());
    check(xcf.has_value(), "xcfEncodeRgba succeeds");
    if (xcf) {
        auto d = xcfDecode(*xcf);
        check(d.has_value(), "xcfDecode reads back the written file");
        check(d && d->width == w && d->height == h, "XCF dimensions intact");
        bool same = d && d->rgba.size() == rgba.size();
        for (std::size_t i = 0; same && i < rgba.size(); ++i)
            same = d->rgba[i] == rgba[i];
        check(same, "XCF pixels round-trip exactly");
    }

    // --- hand-built RAW-compressed file ------------------------------------
    {
        std::string err;
        auto d = xcfDecode(buildRawXcf(), &err);
        check(d.has_value(), "raw-compressed XCF decodes");
        check(d && d->width == 2 && d->height == 2, "raw XCF dimensions intact");
        check(d && d->rgba[0] == 1 * 257 && d->rgba[1] == 5 * 257 && d->rgba[2] == 9 * 257,
              "raw XCF first pixel is R,G,B");
        check(d && d->rgba[3] == 65535, "raw XCF first pixel opaque");
        check(d && d->rgba[3 * 4 + 0] == 4 * 257, "raw XCF last red intact");
    }

    // --- error paths --------------------------------------------------------
    {
        std::string err;
        std::vector<std::uint8_t> junk = {'g', 'i', 'm', 'p', ' ', 'x', 'c', 'f', ' ', 'b', 'a', 'd'};
        check(!xcfDecode(junk, &err).has_value(), "bad header rejected");
        check(!err.empty(), "error message populated");

        auto v2 = buildRawXcf();
        v2[10] = '2';  // "v200"
        check(!xcfDecode(v2, &err).has_value(), "v2xx (16-bit) rejected with a message");
        check(err.find("v2xx") != std::string::npos, "v2xx error names the limitation");
    }

    if (failures == 0) std::printf("OK: XCF RLE round-trip, raw decode, v2xx guard\n");
    return failures == 0 ? 0 : 1;
}