#include "engine/io/xcf.h"

#include <algorithm>
#include <cstring>

namespace pittore::io {
namespace {

constexpr std::uint16_t kMaxSample = 65535;

struct Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t pos = 0;
    bool ok = true;

    explicit Reader(const std::vector<std::uint8_t>& data) : b(data) {}

    std::size_t remaining() const { return b.size() - pos; }

    bool skip(std::size_t n) {
        if (n > remaining()) {
            ok = false;
            return false;
        }
        pos += n;
        return true;
    }

    std::uint8_t u8() { return take(1) ? b[pos - 1] : 0; }

    std::uint32_t u32be() {
        if (!ok || pos + 4 > b.size()) {
            ok = false;
            return 0;
        }
        const std::uint32_t v = (static_cast<std::uint32_t>(b[pos]) << 24) |
                                (static_cast<std::uint32_t>(b[pos + 1]) << 16) |
                                (static_cast<std::uint32_t>(b[pos + 2]) << 8) |
                                static_cast<std::uint32_t>(b[pos + 3]);
        pos += 4;
        return v;
    }

    const std::uint8_t* take(std::size_t n) {
        if (n > remaining()) {
            ok = false;
            return nullptr;
        }
        const std::uint8_t* p = b.data() + pos;
        pos += n;
        return p;
    }
};

std::nullopt_t fail(std::string* error, const char* why) {
    if (error) *error = why;
    return std::nullopt;
}

// XCF "Basis"-style RLE: byte < 128 → a literal run of (byte+1) values;
// byte >= 128 → a repeat of the next value (257 - byte) times. Applies to one
// whole channel (width*height samples). On success, *used receives the source
// index just past the consumed bytes (so consecutive channels decode in
// sequence).
bool rleExpandChannel(const std::vector<std::uint8_t>& src, std::size_t from,
                      std::size_t n, std::uint8_t* out, std::size_t expected,
                      std::size_t* used) {
    std::size_t i = from, o = 0;
    while (i < n && o < expected) {
        const std::uint8_t h = src[i++];
        if (h < 128) {
            const std::size_t c = static_cast<std::size_t>(h) + 1;
            if (i + c > n || o + c > expected) return false;
            std::memcpy(out + o, src.data() + i, c);
            i += c;
            o += c;
        } else {
            const std::size_t c = 257u - h;
            if (i >= n || o + c > expected) return false;
            std::memset(out + o, src[i++], c);
            o += c;
        }
    }
    if (used) *used = i;
    return o == expected;
}

std::vector<std::uint8_t> rlePackChannel(const std::uint8_t* src, std::size_t n) {
    std::vector<std::uint8_t> out;
    std::size_t i = 0;
    while (i < n) {
        std::size_t run = 1;
        while (i + run < n && src[i + run] == src[i] && run < 128) ++run;
        if (run >= 3) {
            while (run > 0) {
                const std::size_t chunk = run < 128 ? run : 128;
                out.push_back(static_cast<std::uint8_t>(257 - chunk));
                out.push_back(src[i]);
                run -= chunk;
                i += chunk;
            }
            continue;
        }
        std::size_t lit = 0;
        while (i + lit < n && lit < 128) {
            std::size_t lr = 1;
            while (i + lit + lr < n && src[i + lit + lr] == src[i + lit] && lr < 128) ++lr;
            if (lr >= 3) break;
            ++lit;
        }
        if (lit == 0) lit = 1;
        out.push_back(static_cast<std::uint8_t>(lit - 1));
        for (std::size_t k = 0; k < lit; ++k) out.push_back(src[i + k]);
        i += lit;
    }
    return out;
}

// A layer/projection entry inside the file's lists. The data at `offset` is
// decoded using `compression` (0 = raw, 1 = RLE) and `type` (XCF layer type).
struct XcfEntry {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t type = 0;
    std::uint32_t compression = 0;
    std::uint32_t offset = 0;
};

int channelCountForType(std::uint32_t type) {
    switch (type) {
        case 0: return 3;   // RGB
        case 1: return 4;   // RGBA
        case 2: return 1;   // grayscale
        case 3: return 2;   // grayscale + alpha
        case 4: return 1;   // indexed
        case 5: return 2;   // indexed + alpha
        default: return 0;
    }
}

// Parses a property list up to (and including) the END property. Captures the
// compression property (name 17) when a pointer is given.
void readProperties(Reader& in, std::uint32_t* compression) {
    for (;;) {
        const std::uint32_t name = in.u32be();
        if (!in.ok) return;
        if (name == 0) return;  // PROP_END
        const std::uint32_t len = in.u32be();
        const std::uint8_t* value = in.take(len);
        if (!in.ok) return;
        if (name == 17 && compression && len >= 4) {
            *compression = (static_cast<std::uint32_t>(value[0]) << 24) |
                           (static_cast<std::uint32_t>(value[1]) << 16) |
                           (static_cast<std::uint32_t>(value[2]) << 8) |
                           static_cast<std::uint32_t>(value[3]);
        }
    }
}

std::string readName(Reader& in) {
    const std::uint32_t len = in.u32be();
    const std::uint8_t* p = in.take(len);
    if (!in.ok) return {};
    std::string s(reinterpret_cast<const char*>(p), len);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::vector<XcfEntry> readEntries(Reader& in, bool hasMask) {
    const std::uint32_t count = in.u32be();
    std::vector<XcfEntry> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        XcfEntry e;
        readName(in);                       // name
        e.width = in.u32be();
        e.height = in.u32be();
        e.type = in.u32be();
        readProperties(in, &e.compression);
        if (hasMask) {
            readName(in);                   // mask name
            (void)in.u32be();               // mask offset
        }
        e.offset = in.u32be();
        if (in.ok) out.push_back(e);
    }
    return out;
}

// Decodes the RGBA composite of one entry. `max` is the sample scale
// (65535 for 8-bit source). Returns false on failure.
bool decodeEntry(const std::vector<std::uint8_t>& file, const XcfEntry& e,
                 const std::vector<std::uint16_t>& colormap, std::vector<std::uint16_t>& rgba) {
    const std::uint64_t q = static_cast<std::uint64_t>(e.width) * e.height;
    if (q == 0 || q > 268435456u) return false;
    const int channels = channelCountForType(e.type);
    if (channels == 0) return false;
    if (e.offset == 0 || e.offset >= file.size()) return false;

    std::vector<std::vector<std::uint8_t>> planes(channels);
    std::size_t cursor = e.offset;
    for (int c = 0; c < channels; ++c) {
        std::vector<std::uint8_t> chan;
        chan.resize(static_cast<std::size_t>(q));
        if (e.compression == 0) {  // raw
            if (cursor + q > file.size()) return false;
            std::memcpy(chan.data(), file.data() + cursor, static_cast<std::size_t>(q));
            cursor += static_cast<std::size_t>(q);
        } else if (e.compression == 1) {  // RLE, one stream per channel
            std::size_t used = cursor;
            if (!rleExpandChannel(file, cursor, file.size(), chan.data(),
                                  static_cast<std::size_t>(q), &used))
                return false;
            cursor = used;
        } else {
            return false;  // unsupported compression (fractal/zip)
        }
        planes[c] = std::move(chan);
    }

    rgba.assign(q * 4, kMaxSample);
    auto scale = [](std::uint8_t v) { return static_cast<std::uint16_t>(v) * 257u; };
    for (std::uint64_t i = 0; i < q; ++i) {
        std::uint16_t r = 0, g = 0, b = 0, a = kMaxSample;
        switch (e.type) {
            case 0:  // RGB
                r = scale(planes[0][i]); g = scale(planes[1][i]); b = scale(planes[2][i]);
                break;
            case 1:  // RGBA
                r = scale(planes[0][i]); g = scale(planes[1][i]); b = scale(planes[2][i]);
                a = scale(planes[3][i]);
                break;
            case 2:  // grayscale
                r = g = b = scale(planes[0][i]);
                break;
            case 3:  // grayscale + alpha
                r = g = b = scale(planes[0][i]);
                a = scale(planes[1][i]);
                break;
            case 4:  // indexed
            case 5: {  // indexed + alpha
                const std::uint16_t idx = planes[0][i];
                std::uint16_t cr = 0, cg = 0, cb = 0;
                if (colormap.size() >= 3) {
                    const std::size_t o = static_cast<std::size_t>(idx) * 3;
                    if (o + 2 < colormap.size()) {
                        cr = colormap[o]; cg = colormap[o + 1]; cb = colormap[o + 2];
                    }
                }
                r = cr; g = cg; b = cb;
                if (e.type == 5) a = scale(planes[1][i]);
                break;
            }
        }
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }
    return true;
}

}  // namespace

std::optional<XcfImage> xcfDecode(const std::vector<std::uint8_t>& data,
                                  std::string* error) {
    Reader in(data);
    const std::uint8_t* magic = in.take(9);
    if (!magic || std::memcmp(magic, "gimp xcf ", 9) != 0)
        return fail(error, "not an XCF file (bad signature)");
    const std::uint8_t* ver = in.take(4);
    if (!ver || ver[0] != 'v') return fail(error, "unsupported XCF version string");
    // v0xx/v1xx carry 8-bit samples; v2xx uses 16-bit/float — not supported.
    if (ver[1] != '0' && ver[1] != '1')
        return fail(error, "16-bit/float XCF (v2xx) is not supported yet");
    if (!in.skip(3)) return fail(error, "truncated XCF header");

    const std::uint32_t width = in.u32be();
    const std::uint32_t height = in.u32be();
    const std::uint32_t baseType = in.u32be();
    if (!in.ok || width == 0 || height == 0)
        return fail(error, "truncated XCF header");

    std::vector<std::uint16_t> colormap;
    // Image properties (capture the indexed-colour palette).
    for (;;) {
        const std::uint32_t name = in.u32be();
        if (!in.ok) return fail(error, "truncated XCF properties");
        if (name == 0) break;
        const std::uint32_t len = in.u32be();
        const std::uint8_t* value = in.take(len);
        if (!in.ok) return fail(error, "truncated XCF properties");
        if (name == 16 && len >= 4) {  // PROP_COLORMAP: u32 count + count*3 RGB
            const std::uint32_t count =
                (static_cast<std::uint32_t>(value[0]) << 24) |
                (static_cast<std::uint32_t>(value[1]) << 16) |
                (static_cast<std::uint32_t>(value[2]) << 8) |
                static_cast<std::uint32_t>(value[3]);
            colormap.reserve(static_cast<std::size_t>(count) * 3);
            for (std::uint32_t i = 0; i < count && 4 + i * 3 + 2 < len; ++i)
                colormap.push_back(static_cast<std::uint16_t>(value[4 + i * 3]) * 257u),
                colormap.push_back(static_cast<std::uint16_t>(value[4 + i * 3 + 1]) * 257u),
                colormap.push_back(static_cast<std::uint16_t>(value[4 + i * 3 + 2]) * 257u);
        }
    }

    const std::vector<XcfEntry> layers = readEntries(in, /*hasMask=*/true);
    if (!in.ok) return fail(error, "truncated XCF layer list");
    // Channel list (alpha masks / selection channels) — structural only.
    static_cast<void>(readEntries(in, /*hasMask=*/false));
    if (!in.ok) return fail(error, "truncated XCF channel list");
    const std::vector<XcfEntry> projections = readEntries(in, /*hasMask=*/true);
    if (!in.ok) return fail(error, "truncated XCF projection list");

    // The flattened composite is the last projection the writer caches; files without
    // projections fall back to their bottom layer.
    const XcfEntry* target = nullptr;
    if (!projections.empty()) {
        target = &projections.back();
    } else if (!layers.empty()) {
        target = &layers.back();
    }
    if (!target) return fail(error, "XCF has no image data");

    XcfImage img;
    img.width = width;
    img.height = height;
    img.baseType = static_cast<int>(baseType);
    img.depth = 8;
    if (!decodeEntry(data, *target, colormap, img.rgba))
        return fail(error, "corrupt XCF image data");
    return img;
}

std::optional<std::vector<std::uint8_t>> xcfEncodeRgba(std::uint32_t width,
                                                       std::uint32_t height,
                                                       const std::uint16_t* rgba) {
    if (width == 0 || height == 0 || !rgba) return std::nullopt;
    const std::uint64_t q = static_cast<std::uint64_t>(width) * height;
    if (q > 268435456u) return std::nullopt;

    std::vector<std::vector<std::uint8_t>> planes(4);
    for (int c = 0; c < 4; ++c) {
        planes[c].resize(q);
        const std::uint16_t* in = rgba + c;
        std::uint8_t* out = planes[c].data();
        for (std::uint64_t i = 0; i < q; ++i, in += 4)
            out[i] = static_cast<std::uint8_t>(std::min<std::uint32_t>(65535u, *in) >> 8);
    }

    auto put32 = [](std::vector<std::uint8_t>& o, std::uint32_t v) {
        o.push_back(static_cast<std::uint8_t>(v >> 24));
        o.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
        o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
        o.push_back(static_cast<std::uint8_t>(v & 0xff));
    };

    std::vector<std::uint8_t> out;
    out.reserve(256 + q);
    const char* magic = "gimp xcf ";
    out.insert(out.end(), magic, magic + 9);
    out.insert(out.end(), {'v', '1', '0', '0'});
    out.insert(out.end(), 3, 0);
    put32(out, width);
    put32(out, height);
    put32(out, 0);  // RGB base type

    put32(out, 0);  // no image properties (END)

    put32(out, 1);  // one layer
    const std::string name = "Layer 1";
    put32(out, static_cast<std::uint32_t>(name.size() + 1));
    out.insert(out.end(), name.begin(), name.end());
    out.push_back('\0');
    put32(out, width);
    put32(out, height);
    put32(out, 1);  // RGBA layer type
    put32(out, 17);  // compression property
    put32(out, 4);
    put32(out, 1);   // RLE
    put32(out, 0);   // END of properties
    put32(out, 0);   // no layer mask (empty name)
    put32(out, 0);   // mask offset
    const std::size_t dataOffsetPos = out.size();
    put32(out, 0);   // layer data offset (patched below)

    put32(out, 0);  // no channels
    put32(out, 0);  // no projections

    const std::uint32_t dataOffset = static_cast<std::uint32_t>(out.size());
    for (int c = 0; c < 4; ++c) {
        std::vector<std::uint8_t> packed = rlePackChannel(planes[c].data(), q);
        out.insert(out.end(), packed.begin(), packed.end());
    }

    // Patch the layer data offset.
    out[dataOffsetPos + 0] = static_cast<std::uint8_t>(dataOffset >> 24);
    out[dataOffsetPos + 1] = static_cast<std::uint8_t>((dataOffset >> 16) & 0xff);
    out[dataOffsetPos + 2] = static_cast<std::uint8_t>((dataOffset >> 8) & 0xff);
    out[dataOffsetPos + 3] = static_cast<std::uint8_t>(dataOffset & 0xff);
    return out;
}

}  // namespace pittore::io