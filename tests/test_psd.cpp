// PSD codecs: flattened-composite RLE round-trip (8 + 16-bit), a hand-built
// raw-compression file, error handling, and the layered codec (groups, blends,
// opacity, visibility, unicode names, 16-bit channels, composite flatten).
#include "engine/compute/adjust.h"
#include "engine/io/psd.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using pittore::io::PsdImage;
using pittore::io::psdDecode;
using pittore::io::psdDecodeLayers;
using pittore::io::psdEncodeLayers;
using pittore::io::psdEncodeRgba;
using pittore::io::PsdLayerCompression;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (cond) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

void put16be(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
}

static std::uint32_t get32be(const std::vector<std::uint8_t>& v,
                             std::size_t p) {
    return (static_cast<std::uint32_t>(v[p]) << 24) |
           (static_cast<std::uint32_t>(v[p + 1]) << 16) |
           (static_cast<std::uint32_t>(v[p + 2]) << 8) |
           static_cast<std::uint32_t>(v[p + 3]);
}

static void set32be(std::vector<std::uint8_t>& v, std::size_t p,
                    std::uint32_t x) {
    v[p] = static_cast<std::uint8_t>(x >> 24);
    v[p + 1] = static_cast<std::uint8_t>((x >> 16) & 0xff);
    v[p + 2] = static_cast<std::uint8_t>((x >> 8) & 0xff);
    v[p + 3] = static_cast<std::uint8_t>(x & 0xff);
}

// Shortens the first '8BIMlevl' block payload to 4 bytes while keeping every
// enclosing length (record extra, layer-info, layer&mask section) consistent.
// Returns false when the layout is not as expected.
bool truncateLevlPayload(std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 40) return false;
    std::size_t pos = 26;
    if (pos + 4 > bytes.size()) return false;
    std::uint32_t cm = get32be(bytes, pos);
    pos += 4 + cm;
    if (pos + 4 > bytes.size()) return false;
    std::uint32_t rl = get32be(bytes, pos);
    pos += 4 + rl;
    if (pos + 4 > bytes.size()) return false;
    const std::size_t maskLenPos = pos;
    std::uint32_t maskLen = get32be(bytes, pos);
    pos += 4;
    if (pos + 4 > bytes.size()) return false;
    const std::size_t infoLenPos = pos;
    std::uint32_t infoLen = get32be(bytes, pos);
    pos += 4;
    if (pos + 2 > bytes.size()) return false;
    const std::uint16_t n =
        static_cast<std::uint16_t>((bytes[pos] << 8) | bytes[pos + 1]);
    pos += 2;
    for (int i = 0; i < n; ++i) {
        if (pos + 18 > bytes.size()) return false;
        pos += 16;  // rect
        std::uint16_t nch =
            static_cast<std::uint16_t>((bytes[pos] << 8) | bytes[pos + 1]);
        pos += 2 + static_cast<std::size_t>(nch) * 6;
        if (pos + 12 > bytes.size()) return false;
        pos += 8;  // signature + key
        pos += 4;  // opacity/clip/flags/filler
        if (pos + 4 > bytes.size()) return false;
        const std::size_t extraLenPos = pos;
        std::uint32_t extraLen = get32be(bytes, pos);
        pos += 4;
        const std::size_t extraEnd = pos + extraLen;
        if (extraEnd > bytes.size()) return false;
        // Scan the extra data for the levl block.
        std::size_t q = pos;
        // Skip mask descriptor + blending ranges + pascal name.
        if (q + 4 > extraEnd) return false;
        std::uint32_t maskField = get32be(bytes, q);
        q += 4 + maskField;
        if (q + 4 > extraEnd) return false;
        std::uint32_t ranges = get32be(bytes, q);
        q += 4 + ranges;
        if (q + 1 > extraEnd) return false;
        std::uint8_t nameLen = bytes[q++];
        q += nameLen;
        while ((q - (extraLenPos + 4)) & 3u) ++q;
        bool done = false;
        while (q + 12 <= extraEnd) {
            const bool isLevl =
                bytes[q] == '8' && bytes[q + 1] == 'B' &&
                bytes[q + 2] == 'I' && bytes[q + 3] == 'M' &&
                bytes[q + 4] == 'l' && bytes[q + 5] == 'e' &&
                bytes[q + 6] == 'v' && bytes[q + 7] == 'l';
            std::uint32_t blkLen = get32be(bytes, q + 8);
            if (q + 12 + blkLen > extraEnd) return false;
            if (isLevl) {
                if (blkLen <= 4) return false;
                const std::uint32_t delta = blkLen - 4;
                set32be(bytes, q + 8, 4);
                bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(q + 12 + 4),
                            bytes.begin() + static_cast<std::ptrdiff_t>(q + 12 + blkLen));
                set32be(bytes, extraLenPos, extraLen - delta);
                set32be(bytes, infoLenPos, infoLen - delta);
                set32be(bytes, maskLenPos, maskLen - delta);
                done = true;
                break;
            }
            q += 12 + blkLen + (blkLen & 1u);
        }
        if (done) return true;
        pos = extraEnd;
    }
    return false;
}

void put32be(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
}

// A minimal RAW-compressed RGB PSD (width 3, height 2 → 4-byte padded rows).
std::vector<std::uint8_t> buildRawPsd() {
    const std::uint8_t r[8] = {1, 2, 3, 0, 4, 5, 6, 0};
    const std::uint8_t g[8] = {10, 11, 12, 0, 13, 14, 15, 0};
    const std::uint8_t b[8] = {20, 21, 22, 0, 23, 24, 25, 0};
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'8', 'B', 'P', 'S'});
    put16be(out, 1);  // version
    out.insert(out.end(), 6, 0);
    put16be(out, 3);  // channels
    put32be(out, 2);  // height
    put32be(out, 3);  // width
    put16be(out, 8);  // depth
    put16be(out, 3);  // RGB
    put32be(out, 0);  // color mode data
    put32be(out, 0);  // image resources
    put32be(out, 0);  // layer + mask info
    put16be(out, 0);  // compression: raw
    out.insert(out.end(), r, r + 8);
    out.insert(out.end(), g, g + 8);
    out.insert(out.end(), b, b + 8);
    return out;
}

// Minimal RAW CMYK PSD (3x1, depth 8): samples stored inverted per spec
// (255 = no ink). P0 paper white, P1 full-ink black, P2 full cyan ink.
std::vector<std::uint8_t> buildCmykRawPsd() {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'8', 'B', 'P', 'S'});
    put16be(out, 1);
    out.insert(out.end(), 6, 0);
    put16be(out, 4);  // C,M,Y,K
    put32be(out, 1);  // height
    put32be(out, 3);  // width
    put16be(out, 8);
    put16be(out, 4);  // CMYK
    put32be(out, 0);  // color mode data
    put32be(out, 0);  // image resources
    put32be(out, 0);  // layer + mask info
    put16be(out, 0);  // compression: raw
    const std::uint8_t c[4] = {255, 0, 0, 0};
    const std::uint8_t m[4] = {255, 0, 255, 0};
    const std::uint8_t y[4] = {255, 0, 255, 0};
    const std::uint8_t k[4] = {255, 0, 255, 0};
    out.insert(out.end(), c, c + 4);
    out.insert(out.end(), m, m + 4);
    out.insert(out.end(), y, y + 4);
    out.insert(out.end(), k, k + 4);
    return out;
}

// Fills a layer's straight RGBA16 samples with a solid 8-bit colour (values
// are exact multiples of 257, so they survive the 8-bit channel round trip).
void fillRgba8(pittore::io::PsdLayerFile& l, int r, int g, int b, int a) {
    l.rgba.assign(static_cast<std::size_t>(l.width) * l.height * 4, 0);
    for (std::size_t i = 0; i < l.rgba.size(); i += 4) {
        l.rgba[i + 0] = static_cast<std::uint16_t>(r) * 257u;
        l.rgba[i + 1] = static_cast<std::uint16_t>(g) * 257u;
        l.rgba[i + 2] = static_cast<std::uint16_t>(b) * 257u;
        l.rgba[i + 3] = static_cast<std::uint16_t>(a) * 257u;
    }
}

pittore::io::PsdLayerFile makePixel(int left, int top, int w, int h,
                                     const char* name, int indent) {    pittore::io::PsdLayerFile l;
    l.left = static_cast<std::uint32_t>(left);
    l.top = static_cast<std::uint32_t>(top);
    l.width = static_cast<std::uint32_t>(w);
    l.height = static_cast<std::uint32_t>(h);
    l.name = name;
    l.indent = indent;
    return l;
}

// Minimal ICC profile with a single 'desc' tag: 'desc' textDescriptionType,
// 'mluc' (enUS) or plain 'text' type around `text`.
std::vector<std::uint8_t> buildIccTag(const std::string& text, char kind) {
    std::vector<std::uint8_t> tag;
    if (kind == 'm') {
        std::vector<std::uint8_t> utf16;
        for (char ch : text) {
            utf16.push_back(0);
            utf16.push_back(static_cast<std::uint8_t>(ch));
        }
        tag.insert(tag.end(), {'m', 'l', 'u', 'c'});
        put32be(tag, 0);
        put32be(tag, 1);   // one record
        put32be(tag, 12);  // record size
        tag.insert(tag.end(), {'e', 'n', 'U', 'S'});
        put32be(tag, static_cast<std::uint32_t>(utf16.size()));
        put32be(tag, 28);  // string offset, relative to the tag start
        tag.insert(tag.end(), utf16.begin(), utf16.end());
    } else if (kind == 't') {
        tag.insert(tag.end(), {'t', 'e', 'x', 't'});
        put32be(tag, 0);
        tag.insert(tag.end(), text.begin(), text.end());
        tag.push_back(0);
    } else {
        tag.insert(tag.end(), {'d', 'e', 's', 'c'});
        put32be(tag, 0);
        put32be(tag, static_cast<std::uint32_t>(text.size()) + 1);
        tag.insert(tag.end(), text.begin(), text.end());
        tag.push_back(0);
    }
    std::vector<std::uint8_t> out(128, 0);  // stock ICC header
    put32be(out, 1);                        // one tag
    out.insert(out.end(), {'d', 'e', 's', 'c'});
    const std::uint32_t tagOff = 128 + 4 + 12;
    put32be(out, tagOff);
    put32be(out, static_cast<std::uint32_t>(tag.size()));
    out.insert(out.end(), tag.begin(), tag.end());
    return out;
}

// Minimal PSD whose image resources hold one 8BIM/0x0422 ICC block.
std::vector<std::uint8_t> buildPsdWithIcc(const std::vector<std::uint8_t>& icc) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'8', 'B', 'P', 'S'});
    put16be(out, 1);
    out.insert(out.end(), 6, 0);
    put16be(out, 3);
    put32be(out, 1);  // height
    put32be(out, 1);  // width
    put16be(out, 8);
    put16be(out, 3);  // RGB
    put32be(out, 0);  // color mode data
    std::vector<std::uint8_t> res;
    res.insert(res.end(), {'8', 'B', 'I', 'M'});
    put16be(res, 0x0422);
    res.push_back(0);
    res.push_back(0);  // empty Pascal name
    put32be(res, static_cast<std::uint32_t>(icc.size()));
    res.insert(res.end(), icc.begin(), icc.end());
    if (icc.size() & 1) res.push_back(0);
    put32be(out, static_cast<std::uint32_t>(res.size()));
    out.insert(out.end(), res.begin(), res.end());
    return out;
}

}  // namespace

int main() {
    // --- 8-bit RLE round-trip ----------------------------------------------
    const std::uint32_t w = 5, h = 3;
    std::vector<std::uint16_t> rgba(w * h * 4);
    for (std::uint32_t i = 0; i < w * h; ++i) {
        rgba[i * 4 + 0] = static_cast<std::uint16_t>((i * 3) % 256);
        rgba[i * 4 + 1] = static_cast<std::uint16_t>((i * 7) % 256);
        rgba[i * 4 + 2] = static_cast<std::uint16_t>((i * 11) % 256);
        rgba[i * 4 + 3] = (i == 0 || i == w * h - 1) ? 0u : 255u;
    }
    auto psd8 = psdEncodeRgba(w, h, 8, rgba.data());
    check(psd8.has_value(), "psdEncodeRgba(8-bit) succeeds");
    PsdImage dec8;
    if (psd8) {
        auto d = psdDecode(*psd8);
        check(d.has_value(), "psdDecode reads back the 8-bit file");
        if (d) {
            dec8 = std::move(*d);
            check(dec8.width == w && dec8.height == h && dec8.colorMode == 3,
                  "8-bit PSD dimensions/color mode intact");
            check(dec8.rgba.size() == rgba.size(), "8-bit PSD sample count intact");
            bool same = dec8.rgba.size() == rgba.size();
            for (std::size_t i = 0; same && i < rgba.size(); ++i)
                same = dec8.rgba[i] == static_cast<std::uint16_t>(rgba[i]) * 257u;
            check(same, "8-bit PSD pixels round-trip exactly");
        }
    }

    // --- 16-bit RLE round-trip ---------------------------------------------
    std::vector<std::uint16_t> rgba16(w * h * 4);
    for (std::uint32_t i = 0; i < w * h; ++i) {
        rgba16[i * 4 + 0] = static_cast<std::uint16_t>((i * 4001) % 65536);
        rgba16[i * 4 + 1] = static_cast<std::uint16_t>((i * 9001) % 65536);
        rgba16[i * 4 + 2] = static_cast<std::uint16_t>((i * 16381) % 65536);
        rgba16[i * 4 + 3] = 65535u;
    }
    auto psd16 = psdEncodeRgba(w, h, 16, rgba16.data());
    check(psd16.has_value(), "psdEncodeRgba(16-bit) succeeds");
    if (psd16) {
        auto d = psdDecode(*psd16);
        check(d.has_value(), "psdDecode reads back the 16-bit file");
        check(d && d->depth == 16, "16-bit depth preserved");
        bool same = d && d->rgba.size() == rgba16.size();
        for (std::size_t i = 0; same && i < rgba16.size(); ++i)
            same = d->rgba[i] == rgba16[i];
        check(same, "16-bit PSD pixels round-trip exactly");
    }

    // --- hand-built RAW-compressed PSD -------------------------------------
    {
        std::vector<std::uint8_t> raw = buildRawPsd();
        std::string err;
        auto d = psdDecode(raw, &err);
        check(d.has_value(), "raw-compressed PSD decodes");
        check(d && d->width == 3 && d->height == 2, "raw PSD dimensions intact");
        check(d && d->rgba[0] == 1 * 257 && d->rgba[1] == 10 * 257 && d->rgba[2] == 20 * 257,
              "raw PSD first pixel is R,G,B");
        check(d && d->rgba[3] == 65535, "raw PSD first pixel opaque");
        check(d && d->rgba[4 * 5 + 0] == 6 * 257, "raw PSD row-2 red intact");
    }

    // --- error paths --------------------------------------------------------
    {
        std::string err;
        std::vector<std::uint8_t> junk = {'N', 'O', 'P', 'E'};
        check(!psdDecode(junk, &err).has_value(), "bad signature rejected");
        check(!err.empty(), "error message populated");
        std::vector<std::uint8_t> trunc = buildRawPsd();
        trunc.resize(trunc.size() - 3);
        check(!psdDecode(trunc, &err).has_value(), "truncated PSD rejected");
    }

    // --- layered document: structure round trip ----------------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 6;
        doc.height = 4;
        doc.depth = 8;
        // File order is bottom → top: bg, Group 1 { a, b, Inner { c } }, top.
        pittore::io::PsdLayerFile bg = makePixel(0, 0, 6, 4, "bg", 0);
        fillRgba8(bg, 255, 0, 0, 255);
        pittore::io::PsdLayerFile g1;
        g1.isGroup = true;
        g1.indent = 0;
        g1.groupExpanded = true;
        g1.name = "Group 1";
        pittore::io::PsdLayerFile a = makePixel(2, 1, 2, 2, "a", 1);
        a.opacity = 128;
        a.blend = "Multiply";
        fillRgba8(a, 0, 0, 255, 255);
        pittore::io::PsdLayerFile b = makePixel(0, 2, 2, 2, "b", 1);
        b.visible = false;
        fillRgba8(b, 0, 255, 0, 255);
        pittore::io::PsdLayerFile inner;
        inner.isGroup = true;
        inner.indent = 1;
        inner.groupExpanded = false;   // closed folder
        inner.name = "Inner";
        pittore::io::PsdLayerFile c = makePixel(5, 0, 1, 1, "c", 2);
        c.blend = "Overlay";
        fillRgba8(c, 255, 255, 255, 255);
        pittore::io::PsdLayerFile top = makePixel(0, 3, 4, 1, "top", 0);
        fillRgba8(top, 255, 255, 0, 255);
        doc.layers = {bg, g1, a, b, inner, c, top};

        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "psdEncodeLayers succeeds on nested groups");
        check(!bytes || err.empty(), "encode error string stays empty on success");
        if (!bytes) return 1;

        auto dec = psdDecodeLayers(*bytes, &err);
        check(dec.has_value(), "psdDecodeLayers reads back the layered file");
        if (dec) {
            check(dec->width == 6 && dec->height == 4 && dec->depth == 8,
                  "layered doc geometry/depth intact");
            check(dec->layers.size() == 7, "all seven layers round-trip");
            if (dec->layers.size() == 7) {
                const auto& L = dec->layers;
                check(L[0].name == "bg" && L[0].indent == 0 && !L[0].isGroup,
                      "bg layer order + indent");
                check(L[1].name == "Group 1" && L[1].isGroup && L[1].indent == 0 &&
                          L[1].groupExpanded,
                      "Group 1 open folder");
                check(L[2].name == "a" && L[2].indent == 1 && L[2].opacity == 128 &&
                          L[2].blend == "Multiply",
                      "a keeps blend + opacity, nested at depth 1");
                check(L[3].name == "b" && L[3].indent == 1 && !L[3].visible,
                      "hidden layer b stays hidden");
                check(L[4].name == "Inner" && L[4].isGroup && L[4].indent == 1 &&
                          !L[4].groupExpanded,
                      "Inner closed folder at depth 1");
                check(L[5].name == "c" && L[5].indent == 2 && L[5].blend == "Overlay",
                      "c nested twice + blend key");
                check(L[6].name == "top" && L[6].indent == 0,
                      "top sibling back at depth 0");
                // Pixel samples survive the 8-bit channel round trip exactly.
                bool aPx = L[2].rgba.size() == 2 * 2 * 4 &&
                           L[2].rgba[0] == 0 && L[2].rgba[1] == 0 &&
                           L[2].rgba[2] == 255 * 257u && L[2].rgba[3] == 255 * 257u;
                check(aPx, "layer pixel data intact for a");
                bool bPx = L[3].rgba.size() == 2 * 2 * 4 &&
                           L[3].rgba[1] == 255 * 257u;
                check(bPx, "hidden layer still carries its pixels");
            }
        }

        // Encoding the decode is idempotent: same file order, same groups.
        if (dec) {
            auto again = psdEncodeLayers(*dec, &err);
            check(again.has_value(), "re-encoding the decoded doc succeeds");
            if (again) {
                auto dec2 = psdDecodeLayers(*again, &err);
                check(dec2.has_value(), "second decode succeeds");
                check(dec2 && dec2->layers.size() == 7, "structure is idempotent");
                bool namesMatch = dec2 && dec2->layers.size() == 7;
                for (int i = 0; namesMatch && i < 7; ++i)
                    namesMatch = dec2->layers[i].name == dec->layers[i].name;
                check(namesMatch, "layer names stable across re-encode");
            }
        }
    }

    // --- layered masks and clipping --------------------------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 4;
        doc.height = 1;
        doc.depth = 8;
        pittore::io::PsdLayerFile base = makePixel(1, 0, 2, 1, "base", 0);
        fillRgba8(base, 255, 0, 0, 255);
        pittore::io::PsdLayerFile top = makePixel(0, 0, 4, 1, "top", 0);
        fillRgba8(top, 0, 0, 255, 255);
        top.clipped = true;
        top.hasMask = true;
        top.maskLeft = 0;
        top.maskTop = 0;
        top.maskWidth = 4;
        top.maskHeight = 1;
        top.mask = {65535u, 65535u, 0u, 0u};
        doc.layers = {base, top};

        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "masked/clipped layered encode succeeds");
        if (bytes) {
            auto dec = psdDecodeLayers(*bytes, &err);
            check(dec.has_value(), "masked/clipped layered decode succeeds");
            if (dec && dec->layers.size() == 2) {
                check(dec->layers[1].clipped, "clipping flag round-trips");
                check(dec->layers[1].hasMask, "mask presence round-trips");
                check(dec->layers[1].maskEnabled, "enabled mask stays enabled");
                check(dec->layers[1].maskWidth == 4 && dec->layers[1].maskHeight == 1,
                      "mask geometry round-trips");
                check(dec->layers[1].mask == top.mask, "mask samples round-trip");
            }
            auto flat = psdDecode(*bytes, &err);
            check(flat.has_value(), "masked/clipped flattened composite decodes");
            if (flat && flat->rgba.size() == 16) {
                // x0: clipped away by the smaller base; x1: blue over red;
                // x2: hidden by the mask; x3: outside the base.
                check(flat->rgba[3] == 0, "clipped pixels vanish outside base");
                check(flat->rgba[4] == 0 && flat->rgba[5] == 0 &&
                          flat->rgba[6] == 65535 && flat->rgba[7] == 65535,
                      "clipped blue shows over the base");
                check(flat->rgba[8] == 65535 && flat->rgba[11] == 65535,
                      "masked-out pixels reveal the base");
                check(flat->rgba[15] == 0, "trailing clipped pixels vanish");
            }
        }
    }

    // --- disabled masks stay editable --------------------------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 2;
        doc.height = 1;
        doc.depth = 8;
        pittore::io::PsdLayerFile base = makePixel(0, 0, 2, 1, "base", 0);
        fillRgba8(base, 255, 0, 0, 255);
        pittore::io::PsdLayerFile top = makePixel(0, 0, 2, 1, "top", 0);
        fillRgba8(top, 0, 0, 255, 255);
        top.hasMask = true;
        top.maskEnabled = false;
        top.maskLeft = 0;
        top.maskTop = 0;
        top.maskWidth = 2;
        top.maskHeight = 1;
        top.mask = {0u, 0u};
        doc.layers = {base, top};

        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "disabled-mask encode succeeds");
        if (bytes) {
            auto dec = psdDecodeLayers(*bytes, &err);
            check(dec.has_value() && dec->layers.size() == 2 &&
                      dec->layers[1].hasMask && !dec->layers[1].maskEnabled,
                  "disabled mask round-trips as disabled");
            auto flat = psdDecode(*bytes, &err);
            check(flat.has_value() && flat->rgba.size() == 8 &&
                      flat->rgba[2] == 65535,
                  "disabled mask does not hide its layer");
        }
    }

    // --- unicode layer names ------------------------------------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 2;
        doc.height = 2;
        doc.depth = 8;
        pittore::io::PsdLayerFile l = makePixel(0, 0, 2, 2, "", 0);
        l.name = "L\xc3\xa4yer \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";  // "Läyer 日本語"
        fillRgba8(l, 1, 2, 3, 255);
        doc.layers = {l};
        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "encode succeeds with a unicode name");
        if (bytes) {
            auto dec = psdDecodeLayers(*bytes, &err);
            check(dec && dec->layers.size() == 1 &&
                      dec->layers[0].name == l.name,
                  "unicode layer name survives via luni");
        }
    }

    // --- 16-bit layered channels -------------------------------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 3;
        doc.height = 2;
        doc.depth = 16;
        pittore::io::PsdLayerFile l = makePixel(0, 0, 3, 2, "p", 1);
        for (std::size_t i = 0; i < 3u * 2 * 4; ++i)
            l.rgba.push_back(static_cast<std::uint16_t>((i * 4001) % 65536));
        pittore::io::PsdLayerFile g;
        g.isGroup = true;
        g.indent = 0;
        g.name = "G";
        doc.layers = {g, l};   // group opens, then its child at depth 1
        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "16-bit layered encode succeeds");
        if (bytes) {
            auto dec = psdDecodeLayers(*bytes, &err);
            check(dec.has_value() && dec->depth == 16, "16-bit depth preserved");
            check(dec && dec->layers.size() == 2 && dec->layers[0].isGroup &&
                      dec->layers[0].name == "G" && !dec->layers[1].isGroup &&
                      dec->layers[1].name == "p" && dec->layers[1].indent == 1,
                  "16-bit layered structure intact");
            bool same = dec && dec->layers[1].rgba.size() == l.rgba.size();
            for (std::size_t i = 0; same && i < l.rgba.size(); ++i)
                same = dec->layers[1].rgba[i] == l.rgba[i];
            check(same, "16-bit layer pixels round-trip exactly");
        }
    }

    // --- flattened composite from layered source ---------------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 2;
        doc.height = 1;
        doc.depth = 8;
        pittore::io::PsdLayerFile bg = makePixel(0, 0, 2, 1, "bg", 0);
        fillRgba8(bg, 255, 0, 0, 255);
        pittore::io::PsdLayerFile mid = makePixel(0, 0, 2, 1, "mid", 0);
        mid.opacity = 128;                       // 128/255 blend → see math below
        fillRgba8(mid, 0, 0, 255, 255);          // blue at ~50%
        pittore::io::PsdLayerFile hidden = makePixel(0, 0, 2, 1, "hidden", 0);
        hidden.visible = false;                  // must not contribute
        fillRgba8(hidden, 255, 255, 255, 255);
        doc.layers = {bg, mid, hidden};
        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "composite-encode succeeds");
        if (bytes) {
            auto flat = psdDecode(*bytes, &err);
            // psdDecode scales 8-bit samples back to the full 16-bit range.
            // The composite of red@100% then blue@(128/255, Normal) is exactly
            // R=(127/255)*65535 = 32639, B=(128/255)*65535 = 32896.
            check(flat.has_value(), "flattened composite decodes");
            if (flat) {
                check(flat->rgba[0] == 32639 && flat->rgba[1] == 0 &&
                          flat->rgba[2] == 32896 && flat->rgba[3] == 65535,
                      "composite = red over 50% blue, hidden layer ignored");
            }
        }
    }

    // --- live adjustment layers ------------------------------------------
    // Every kind round-trips (params, curve points, flags, geometry), one is
    // clipped, one is masked, two nest in a group, and re-encoding the
    // decode is byte-identical.
    {
        auto makeAdj = [](const char* name, int kind, int indent) {
            pittore::io::PsdLayerFile l;
            l.left = 0;
            l.top = 0;
            l.width = 4;
            l.height = 1;
            l.name = name;
            l.indent = indent;
            l.isAdjustment = true;
            l.adjustmentKind = kind;
            return l;
        };
        pittore::io::PsdLayersDoc doc;
        doc.width = 4;
        doc.height = 1;
        doc.depth = 8;
        pittore::io::PsdLayerFile bg = makePixel(0, 0, 4, 1, "bg", 0);
        fillRgba8(bg, 255, 0, 0, 255);
        doc.layers.push_back(bg);
        pittore::io::PsdLayerFile bc = makeAdj("bc", 1, 0);
        bc.adjustmentParams[0] = 0.2f;
        bc.adjustmentParams[1] = -0.1f;
        doc.layers.push_back(bc);
        pittore::io::PsdLayerFile lv = makeAdj("lv", 2, 0);
        lv.adjustmentParams[0] = 0.2f;
        lv.adjustmentParams[1] = 1.0f;
        lv.adjustmentParams[2] = 1.5f;
        lv.adjustmentParams[3] = 0.0f;
        lv.adjustmentParams[4] = 1.0f;
        doc.layers.push_back(lv);
        pittore::io::PsdLayerFile cu = makeAdj("cu", 3, 0);
        cu.adjustmentCurve = {{0.0, 0.0}, {0.5, 0.75}, {1.0, 1.0}};
        cu.adjustmentCurveR = {{0.0, 0.0}, {1.0, 0.5}};
        doc.layers.push_back(cu);
        pittore::io::PsdLayerFile ex = makeAdj("ex", 4, 0);
        ex.adjustmentParams[0] = 1.0f;
        doc.layers.push_back(ex);
        pittore::io::PsdLayerFile vi = makeAdj("vi", 5, 0);
        vi.adjustmentParams[0] = 0.5f;
        vi.hasMask = true;
        vi.maskLeft = 0;
        vi.maskTop = 0;
        vi.maskWidth = 4;
        vi.maskHeight = 1;
        vi.mask = {65535u, 65535u, 0u, 0u};  // left half only
        doc.layers.push_back(vi);
        pittore::io::PsdLayerFile hs = makeAdj("hs", 6, 0);
        hs.adjustmentParams[0] = 30.0f;
        hs.adjustmentParams[1] = 0.2f;
        hs.adjustmentParams[2] = -0.1f;
        doc.layers.push_back(hs);
        pittore::io::PsdLayerFile nv = makeAdj("nv", 7, 0);
        nv.clipped = true;  // clips to the red base below
        doc.layers.push_back(nv);
        pittore::io::PsdLayerFile th = makeAdj("th", 8, 0);
        th.adjustmentParams[0] = 0.5f;
        doc.layers.push_back(th);
        pittore::io::PsdLayerFile g;
        g.isGroup = true;
        g.indent = 0;
        g.name = "G";
        doc.layers.push_back(g);
        pittore::io::PsdLayerFile po = makeAdj("po", 9, 1);
        po.adjustmentParams[0] = 4.0f;
        doc.layers.push_back(po);

        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "adjustment layered encode succeeds");
        if (!bytes) return 1;
        auto dec = psdDecodeLayers(*bytes, &err);
        check(dec.has_value(), "adjustment layered decode succeeds");
        if (!dec || dec->layers.size() != doc.layers.size()) {
            check(false, "adjustment layer count round-trips");
            return 1;
        }
        const auto& L = dec->layers;
        check(L[1].isAdjustment && L[1].adjustmentKind == 1 &&
                  L[1].adjustmentParams[0] == 0.2f &&
                  L[1].adjustmentParams[1] == -0.1f,
              "brightness/contrast params round-trip");
        check(L[2].adjustmentKind == 2 && L[2].adjustmentParams[2] == 1.5f,
              "levels params round-trip");
        bool curveOk = L[3].adjustmentKind == 3 &&
                       L[3].adjustmentCurve.size() == 3;
        for (std::size_t k = 0; curveOk && k < 3; ++k) {
            const double ex_[3] = {0.0, 0.5, 1.0};
            const double ey[3] = {0.0, 0.75, 1.0};
            if (std::abs(L[3].adjustmentCurve[k].first - ex_[k]) > 1.0 / 255 ||
                std::abs(L[3].adjustmentCurve[k].second - ey[k]) > 1.0 / 255)
                curveOk = false;
        }
        check(curveOk, "curves control points round-trip");
        // The R channel rides in the same block (bitmap bit 1); G/B stay
        // empty (identity).
        bool curveROk = L[3].adjustmentCurveR.size() == 2 &&
                        L[3].adjustmentCurveG.empty() &&
                        L[3].adjustmentCurveB.empty();
        if (curveROk) {
            curveROk =
                std::abs(L[3].adjustmentCurveR[1].first - 1.0) < 1.0 / 255 &&
                std::abs(L[3].adjustmentCurveR[1].second - 0.5) < 1.0 / 255;
        }
        check(curveROk, "curves red channel round-trips");
        check(L[4].adjustmentKind == 4 && L[4].adjustmentParams[0] == 1.0f,
              "exposure params round-trip");
        check(L[5].adjustmentKind == 5 && L[5].hasMask &&
                  L[5].mask == vi.mask,
              "vibrance params + mask round-trip");
        check(L[6].adjustmentKind == 6 && L[6].adjustmentParams[0] == 30.0f &&
                  L[6].adjustmentParams[1] == 0.2f &&
                  L[6].adjustmentParams[2] == -0.1f,
              "hue/saturation params round-trip");
        check(L[7].adjustmentKind == 7 && L[7].clipped,
              "invert + clipping flag round-trip");
        check(L[8].adjustmentKind == 8, "threshold kind round-trips");
        check(std::abs(L[8].adjustmentParams[0] - 128.0 / 255.0) < 1e-6,
              "threshold level round-trips");
        check(L[10].adjustmentKind == 9 && L[10].indent == 1 &&
                  L[10].adjustmentParams[0] == 4.0f,
              "posterize in group round-trips");
        // Re-encoding the decode is byte-identical: deterministic RLE,
        // records and blocks throughout.
        auto again = psdEncodeLayers(*dec, &err);
        bool identical = again.has_value() && *again == *bytes;
        if (!identical && again.has_value()) {
            std::printf("    idempotence: %zu vs %zu bytes", again->size(),
                        bytes->size());
            const std::size_t m =
                std::min(again->size(), bytes->size());
            for (std::size_t k = 0; k < m; ++k) {
                if ((*again)[k] != (*bytes)[k]) {
                    std::printf(", first diff at %zu (again=%u bytes=%u)", k,
                                (*again)[k], (*bytes)[k]);
                    break;
                }
            }
            std::printf("\n");
        }
        check(identical, "adjustment encode is byte-idempotent");
    }

    // --- adjustment flattened preview --------------------------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 2;
        doc.height = 1;
        doc.depth = 8;
        pittore::io::PsdLayerFile bg = makePixel(0, 0, 2, 1, "bg", 0);
        fillRgba8(bg, 255, 0, 0, 255);
        pittore::io::PsdLayerFile inv;
        inv.left = 0;
        inv.top = 0;
        inv.width = 2;
        inv.height = 1;
        inv.name = "inv";
        inv.isAdjustment = true;
        inv.adjustmentKind = 7;
        doc.layers = {bg, inv};
        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "invert-preview encode succeeds");
        if (bytes) {
            auto flat = psdDecode(*bytes, &err);
            check(flat.has_value(), "invert-preview composite decodes");
            if (flat && flat->rgba.size() == 8) {
                // Red inverted at full strength over opaque red is exactly
                // cyan: the blend runs at source alpha 1.
                check(flat->rgba[0] == 0 && flat->rgba[1] == 65535 &&
                          flat->rgba[2] == 65535 && flat->rgba[3] == 65535,
                      "invert preview = cyan");
            }
        }
    }

    // --- foreign adjustment shapes degrade to stand-ins --------------------
    // A truncated 'levl' payload must not fail the file: the row imports as
    // a visible no-op (kind 0) instead of misparsed pixels.
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 2;
        doc.height = 1;
        doc.depth = 8;
        pittore::io::PsdLayerFile bg = makePixel(0, 0, 2, 1, "bg", 0);
        fillRgba8(bg, 255, 0, 0, 255);
        pittore::io::PsdLayerFile lv;
        lv.left = 0;
        lv.top = 0;
        lv.width = 2;
        lv.height = 1;
        lv.name = "lv";
        lv.isAdjustment = true;
        lv.adjustmentKind = 2;
        lv.adjustmentParams[0] = 0.0f;
        lv.adjustmentParams[1] = 1.0f;
        lv.adjustmentParams[2] = 1.0f;
        lv.adjustmentParams[3] = 0.0f;
        lv.adjustmentParams[4] = 1.0f;
        doc.layers = {bg, lv};
        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "truncation fixture encodes");
        if (bytes) {
            check(truncateLevlPayload(*bytes), "levl block truncated");
            {
                auto dec = psdDecodeLayers(*bytes, &err);
                check(dec.has_value() && dec->layers.size() == 2,
                      "truncated levl still parses");
                if (dec && dec->layers.size() == 2) {
                    check(dec->layers[1].isAdjustment &&
                              dec->layers[1].adjustmentKind == 0,
                          "truncated levl becomes a stand-in");
                }
            }
        }
    }

    // --- 'hue2' reads like 'hust' ------------------------------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 2;
        doc.height = 1;
        doc.depth = 8;
        pittore::io::PsdLayerFile bg = makePixel(0, 0, 2, 1, "bg", 0);
        fillRgba8(bg, 255, 0, 0, 255);
        pittore::io::PsdLayerFile hs;
        hs.left = 0;
        hs.top = 0;
        hs.width = 2;
        hs.height = 1;
        hs.name = "hs";
        hs.isAdjustment = true;
        hs.adjustmentKind = 6;
        hs.adjustmentParams[0] = 30.0f;
        doc.layers = {bg, hs};
        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "hue2 fixture encodes");
        if (bytes) {
            const char* sig = "8BIMhust";
            bool found = false;
            for (std::size_t k = 0; k + 8 <= bytes->size(); ++k) {
                if (std::memcmp(bytes->data() + k, sig, 8) == 0) {
                    std::memcpy(bytes->data() + k + 4, "hue2", 4);
                    found = true;
                    break;
                }
            }
            check(found, "hust block found");
            if (found) {
                auto dec = psdDecodeLayers(*bytes, &err);
                check(dec.has_value() && dec->layers.size() == 2 &&
                          dec->layers[1].isAdjustment &&
                          dec->layers[1].adjustmentKind == 6 &&
                          dec->layers[1].adjustmentParams[0] == 30.0f,
                      "hue2 key accepted with hust layout");
            }
        }
    }

    // --- adjustment kind ints match the engine enum --------------------------
    {
        using pittore::compute::AdjustmentKind;
        check(static_cast<int>(AdjustmentKind::None) == 0, "kind None = 0");
        check(static_cast<int>(AdjustmentKind::BrightnessContrast) == 1,
              "kind BC = 1");
        check(static_cast<int>(AdjustmentKind::Levels) == 2, "kind Levels = 2");
        check(static_cast<int>(AdjustmentKind::Curves) == 3, "kind Curves = 3");
        check(static_cast<int>(AdjustmentKind::Exposure) == 4,
              "kind Exposure = 4");
        check(static_cast<int>(AdjustmentKind::Vibrance) == 5,
              "kind Vibrance = 5");
        check(static_cast<int>(AdjustmentKind::HueSaturation) == 6,
              "kind HueSat = 6");
        check(static_cast<int>(AdjustmentKind::Invert) == 7, "kind Invert = 7");
        check(static_cast<int>(AdjustmentKind::Threshold) == 8,
              "kind Threshold = 8");
        check(static_cast<int>(AdjustmentKind::Posterize) == 9,
              "kind Posterize = 9");
    }

    // --- embedded ICC profile name ---------------------------------------
    {
        using pittore::io::psdIccProfileName;
        check(psdIccProfileName(buildPsdWithIcc(buildIccTag("Test RGB", 'd'))) ==
                  "Test RGB",
              "ICC 'desc' description read from 0x0422 resource");
        check(psdIccProfileName(buildPsdWithIcc(buildIccTag("Test MLUC", 'm'))) ==
                  "Test MLUC",
              "ICC 'mluc' enUS description read");
        check(psdIccProfileName(buildPsdWithIcc(buildIccTag("Test TEXT", 't'))) ==
                  "Test TEXT",
              "ICC 'text' description read");
        check(psdIccProfileName(buildRawPsd()).empty(),
              "no resources means no profile");
        auto trunc = buildPsdWithIcc(buildIccTag("X", 'd'));
        trunc.resize(40);
        check(psdIccProfileName(trunc).empty(), "truncated file yields nothing");
        check(psdIccProfileName({'N', 'O', 'P', 'E'}).empty(),
              "non-PSD yields nothing");
        auto hostile = buildPsdWithIcc(buildIccTag(std::string("\xff\xfe", 2), 'd'));
        check(psdIccProfileName(hostile).empty(),
              "non-ASCII description rejected");
    }

    // --- spec-shaped CMYK composite --------------------------------------
    {
        auto d = psdDecode(buildCmykRawPsd());
        check(d.has_value(), "CMYK composite decodes");
        if (d) {
            check(d->colorMode == 4, "CMYK mode preserved in decode");
            check(d->rgba.size() == 3 * 4, "CMYK pixel count intact");
            auto px = [&](int i) { return &d->rgba[static_cast<std::size_t>(i) * 4]; };
            check(px(0)[0] == 65535 && px(0)[1] == 65535 && px(0)[2] == 65535,
                  "paper white (stored 255s) decodes white, not inverted");
            check(px(1)[0] == 0 && px(1)[1] == 0 && px(1)[2] == 0,
                  "full ink (stored 0s) decodes black");
            check(px(2)[0] == 0 && px(2)[1] == 65535 && px(2)[2] == 65535,
                  "full cyan ink decodes (0, max, max)");
        }
        // Profiled decode through a real coated profile when one exists.
        {
            FILE* f = std::fopen(
                "/usr/share/ghostscript/iccprofiles/default_cmyk.icc", "rb");
            if (!f) {
                std::printf("  (skip: no ghostscript CMYK fixture)\n");
            } else {
                std::vector<std::uint8_t> icc;
                int ch = 0;
                while ((ch = std::fgetc(f)) != EOF)
                    icc.push_back(static_cast<std::uint8_t>(ch));
                std::fclose(f);
                std::string err;
                auto d = psdDecode(buildCmykRawPsd(), &err, &icc);
                check(d.has_value(), "profiled CMYK composite decodes");
                if (d && d->rgba.size() == 3 * 4) {
                    auto px = [&](int i) {
                        return &d->rgba[static_cast<std::size_t>(i) * 4];
                    };
                    check(px(0)[0] > 55000 && px(0)[1] > 55000 &&
                              px(0)[2] > 55000,
                          "profiled paper white stays white");
                    check(px(1)[0] < 20000 && px(1)[1] < 20000 &&
                              px(1)[2] < 20000,
                          "profiled full ink stays near-black");
                }
            }
        }
    }

    // --- foreign blocks ride verbatim (sig, odd pad, >1MB) ---------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 2;
        doc.height = 1;
        doc.depth = 8;
        pittore::io::PsdLayerFile bg = makePixel(0, 0, 2, 1, "bg", 0);
        fillRgba8(bg, 255, 0, 0, 255);
        // Odd-sized 8B64 block exercises the exact-pad path.
        pittore::io::PsdLayerFile::RawBlock tysh;
        std::memcpy(tysh.sig, "8B64", 4);
        std::memcpy(tysh.key, "TySh", 4);
        tysh.data = {1, 2, 3, 4, 5, 6, 7};
        bg.rawBlocks.push_back(tysh);
        // 2MB block proves the old 1MB cap is gone.
        pittore::io::PsdLayerFile::RawBlock sold;
        std::memcpy(sold.sig, "8BIM", 4);
        std::memcpy(sold.key, "SoLd", 4);
        sold.data.assign(2u << 20, 0xab);
        bg.rawBlocks.push_back(sold);
        doc.layers = {bg};
        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "verbatim-block encode succeeds");
        if (bytes) {
            auto dec = psdDecodeLayers(*bytes, &err);
            check(dec.has_value() && dec->layers.size() == 1 &&
                      dec->layers[0].rawBlocks.size() == 2,
                  "both foreign blocks survive decode");
            if (dec && dec->layers.size() == 1 &&
                dec->layers[0].rawBlocks.size() == 2) {
                const auto& b0 = dec->layers[0].rawBlocks[0];
                check(std::memcmp(b0.sig, "8B64", 4) == 0 &&
                          std::memcmp(b0.key, "TySh", 4) == 0 &&
                          b0.data == tysh.data,
                      "8B64 sig + odd data preserved");
                const auto& b1 = dec->layers[0].rawBlocks[1];
                check(std::memcmp(b1.sig, "8BIM", 4) == 0 &&
                          std::memcmp(b1.key, "SoLd", 4) == 0 &&
                          b1.data.size() == (2u << 20),
                      "2MB block preserved");
            }
            auto again = psdEncodeLayers(*dec, &err);
            check(again.has_value() && *again == *bytes,
                  "verbatim re-encode is byte-identical");
        }
    }

    // --- ZIP encode round-trips through our own decoder --------------------
    {
        pittore::io::PsdLayersDoc doc;
        doc.width = 6;
        doc.height = 4;
        doc.depth = 8;
        pittore::io::PsdLayerFile bg = makePixel(0, 0, 6, 4, "bg", 0);
        fillRgba8(bg, 10, 200, 30, 255);
        pittore::io::PsdLayerFile fg = makePixel(1, 1, 3, 2, "fg", 0);
        fillRgba8(fg, 250, 5, 140, 255);
        fg.hasMask = true;
        fg.maskLeft = 0;
        fg.maskTop = 0;
        fg.maskWidth = 3;
        fg.maskHeight = 2;
        fg.mask = {65535u, 0u, 65535u, 0u, 65535u, 0u};
        doc.layers = {bg, fg};
        std::string err;
        auto bytes =
            psdEncodeLayers(doc, &err, PsdLayerCompression::Zip);
        check(bytes.has_value(), "ZIP layered encode succeeds");
        if (bytes) {
            // A different code path ran: same content, different bytes.
            auto rle = psdEncodeLayers(doc, &err, PsdLayerCompression::Rle);
            check(rle.has_value() && *rle != *bytes,
                  "ZIP encoding differs from RLE");
            auto dec = psdDecodeLayers(*bytes, &err);
            check(dec.has_value() && dec->layers.size() == 2,
                  "ZIP layered decode succeeds");
            if (dec && dec->layers.size() == 2) {
                check(dec->layers[0].rgba == bg.rgba,
                      "ZIP layer pixels round-trip");
                check(dec->layers[1].mask == fg.mask,
                      "ZIP mask samples round-trip");
            }
            auto flat = psdDecode(*bytes, &err);
            check(flat.has_value(), "ZIP composite decodes");
            auto again =
                psdEncodeLayers(*dec, &err, PsdLayerCompression::Zip);
            check(again.has_value() && *again == *bytes,
                  "ZIP re-encode is byte-identical");
        }
        // Per-layer override: preferZip wins under a global RLE encode.
        {
            pittore::io::PsdLayersDoc doc2;
            doc2.width = 6;
            doc2.height = 4;
            doc2.depth = 8;
            pittore::io::PsdLayerFile bg = makePixel(0, 0, 6, 4, "bg", 0);
            fillRgba8(bg, 10, 200, 30, 255);
            pittore::io::PsdLayerFile fg = makePixel(1, 1, 3, 2, "fg", 0);
            fillRgba8(fg, 250, 5, 140, 255);
            fg.preferZip = true;
            doc2.layers = {bg, fg};
            std::string err;
            auto bytes = psdEncodeLayers(doc2, &err, PsdLayerCompression::Rle);
            check(bytes.has_value(), "preferZip encode succeeds");
            if (bytes) {
                pittore::io::PsdLayersDoc docRle = doc2;
                docRle.layers[1].preferZip = false;
                auto allRle = psdEncodeLayers(docRle, &err);
                check(allRle.has_value() && *allRle != *bytes,
                      "preferZip changes the bytes");
                auto dec = psdDecodeLayers(*bytes, &err);
                check(dec.has_value() && dec->layers.size() == 2,
                      "preferZip decode succeeds");
                if (dec && dec->layers.size() == 2)
                    check(dec->layers[1].rgba == fg.rgba,
                          "preferZip pixels round-trip");
            }
        }
    }

    // --- CMYK write: separation, inversion, embedded ICC -------------------
    {
        using pittore::io::psdEmbeddedIcc;
        using pittore::io::psdIccProfileName;
        auto hdrChannels = [](const std::vector<std::uint8_t>& b) {
            return (static_cast<int>(b[12]) << 8) | b[13];
        };
        auto hdrMode = [](const std::vector<std::uint8_t>& b) {
            return (static_cast<int>(b[24]) << 8) | b[25];
        };
        auto near16 = [](std::uint16_t got, int want, int tol) {
            const int d = static_cast<int>(got) - want;
            return d < tol && d > -tol;
        };

        // Naive separation (no ICC): primaries and neutrals survive
        // appearance -> inks -> appearance, and the header declares the CMYK
        // shape the PSD format expects (mode id 4, five channels incl. the
        // transparency plane).
        pittore::io::PsdLayersDoc doc;
        doc.width = 8;
        doc.height = 8;
        doc.depth = 8;
        doc.colorMode = 4;
        pittore::io::PsdLayerFile l = makePixel(0, 0, 8, 8, "art", 0);
        l.rgba.assign(8u * 8u * 4, 0);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * 8 + x) * 4;
                std::uint16_t r = 0, g = 0, b = 0;
                if (x < 4 && y < 4) {
                    r = 65535;  // red patch
                } else if (x >= 4 && y < 4) {
                    r = g = b = 65535;  // white patch
                } else if (x < 4) {
                    r = g = b = 32768;  // mid gray patch
                }  // else black patch
                l.rgba[i + 0] = r;
                l.rgba[i + 1] = g;
                l.rgba[i + 2] = b;
                l.rgba[i + 3] = 65535;
            }
        doc.layers = {l};
        std::string err;
        auto bytes = psdEncodeLayers(doc, &err);
        check(bytes.has_value(), "CMYK layered encode succeeds");
        if (bytes) {
            check(hdrChannels(*bytes) == 5, "CMYK header: 5 channels (CMYK+A)");
            check(hdrMode(*bytes) == 4, "CMYK header: color mode 4");
            auto d = psdDecode(*bytes, &err);
            check(d.has_value() && d->colorMode == 4,
                  "CMYK composite decodes mode 4");
            if (d && d->rgba.size() == 64u * 4) {
                const std::uint16_t* red = &d->rgba[0];
                check(near16(red[0], 65535, 512) && near16(red[1], 0, 512) &&
                          near16(red[2], 0, 512),
                      "red patch survives CMYK round trip");
                const std::uint16_t* wht = &d->rgba[5 * 4];
                check(near16(wht[0], 65535, 512) && near16(wht[1], 65535, 512) &&
                          near16(wht[2], 65535, 512),
                      "white patch stays white");
                const std::uint16_t* gry = &d->rgba[32 * 4];
                check(near16(gry[0], 32768, 512) && near16(gry[1], 32768, 512) &&
                          near16(gry[2], 32768, 512),
                      "mid gray stays neutral");
                const std::uint16_t* blk = &d->rgba[63 * 4];
                check(near16(blk[0], 0, 512) && near16(blk[1], 0, 512) &&
                          near16(blk[2], 0, 512),
                      "black patch stays black");
            }
            auto dec = psdDecodeLayers(*bytes, &err);
            check(dec.has_value() && dec->layers.size() == 1,
                  "CMYK layered decode succeeds");
            if (dec && dec->layers.size() == 1) {
                check(dec->colorMode == 4,
                      "layered reader reports the file's CMYK mode");
                check(dec->icc.empty(), "no profile passed -> no icc kept");
                const auto& px = dec->layers[0].rgba;
                check(px.size() == 64u * 4 && px[0] == 65535 && px[1] == 0 &&
                          px[2] == 0,
                      "layer red round-trips through inks");
            }
            check(psdEmbeddedIcc(*bytes).empty(), "no ICC -> no resource block");
        }

        // Profiled write (Ghostscript fixture): the profile rides back out at
        // the spec resource id 0x040F and the separation is the profile's.
        FILE* f = std::fopen(
            "/usr/share/ghostscript/iccprofiles/default_cmyk.icc", "rb");
        if (!f) {
            std::printf("  (skip: no ghostscript CMYK fixture for write)\n");
        } else {
            std::vector<std::uint8_t> icc;
            int c = 0;
            while ((c = std::fgetc(f)) != EOF)
                icc.push_back(static_cast<std::uint8_t>(c));
            std::fclose(f);
            pittore::io::PsdLayersDoc d2 = doc;
            d2.icc = icc;
            auto b2 = psdEncodeLayers(d2, &err);
            check(b2.has_value(), "profiled CMYK layered encode succeeds");
            if (b2) {
                check(psdEmbeddedIcc(*b2) == icc,
                      "ICC bytes round-trip through resource 0x040F");
                check(!psdIccProfileName(*b2).empty(),
                      "embedded ICC description reads back");
                // The layered reader reports the file's real mode and hands
                // back the exact profile bytes it decoded through, so the
                // importer can retag and re-export through the same
                // separation.
                auto ldec = psdDecodeLayers(*b2, &err, &icc);
                check(ldec.has_value() && ldec->colorMode == 4,
                      "layered reader reports CMYK mode");
                check(ldec && ldec->icc == icc,
                      "layered reader retains the ICC bytes");
                auto d = psdDecode(*b2, &err, &icc);
                check(d.has_value() && d->colorMode == 4,
                      "profiled CMYK composite decodes");
                if (d && d->rgba.size() == 64u * 4) {
                    const std::uint16_t* wht = &d->rgba[5 * 4];
                    check(wht[0] > 55000 && wht[1] > 55000 && wht[2] > 55000,
                          "profiled white stays white");
                    const std::uint16_t* blk = &d->rgba[63 * 4];
                    check(blk[0] < 20000 && blk[1] < 20000 && blk[2] < 20000,
                          "profiled black stays near-black");
                }
                // The flat writer takes the same mode/profile.
                auto flat = psdEncodeRgba(8, 8, 8, l.rgba.data(), 4,
                                          icc.data(), icc.size());
                check(flat.has_value(), "flat CMYK encode succeeds");
                if (flat) {
                    check(hdrMode(*flat) == 4, "flat CMYK header: mode 4");
                    check(hdrChannels(*flat) == 5,
                          "flat CMYK header: 5 channels");
                    check(psdEmbeddedIcc(*flat) == icc, "flat CMYK embeds ICC");
                    auto fd = psdDecode(*flat, &err, &icc);
                    check(fd.has_value() && fd->colorMode == 4,
                          "flat CMYK decodes mode 4");
                }
            }
        }
    }

    if (failures == 0)
        std::printf("OK: PSD 8+16-bit RLE round trip, raw decode, errors, layered groups/blends/16-bit/composite, icc-name, verbatim-blocks, zip-encode, cmyk-write\n");
    return failures == 0 ? 0 : 1;
}