#include "engine/compute/brushes/loaders/loaders.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pittore::compute::brushload {
namespace {

constexpr std::uint32_t kMaxDim = 1024;
constexpr std::size_t kMaxTips = 512;
constexpr std::uint32_t kTipMagic = 0x47494D50u;  // file magic, big-endian

struct Cursor {
    const std::uint8_t* p;
    std::size_t n;
    std::size_t pos = 0;
    bool ok = true;

    bool need(std::size_t k) {
        if (!ok || pos + k > n || pos + k < pos) {
            ok = false;
            return false;
        }
        return true;
    }
    std::uint8_t u8() {
        if (!need(1)) return 0;
        return p[pos++];
    }
    std::uint16_t u16be() {
        if (!need(2)) return 0;
        const std::uint16_t v =
            (std::uint16_t(p[pos]) << 8) | std::uint16_t(p[pos + 1]);
        pos += 2;
        return v;
    }
    std::uint32_t u32be() {
        if (!need(4)) return 0;
        const std::uint32_t v =
            (std::uint32_t(p[pos]) << 24) | (std::uint32_t(p[pos + 1]) << 16) |
            (std::uint32_t(p[pos + 2]) << 8) | std::uint32_t(p[pos + 3]);
        pos += 4;
        return v;
    }
    void skip(std::size_t k) {
        if (!need(k)) return;
        pos += k;
    }
};

// PackBits stream decode into a w*h mask with row clipping: runs that
// would overflow a row are clipped (encoders in the wild emit them;
// reference decoders clip and zero-pad instead of failing). Bounded by
// `end`; never reads past it. Returns false on truncation.
bool stream_decode(Cursor& c, std::size_t end, std::uint8_t* mask,
                   std::int64_t w, std::int64_t h, std::size_t stride) {
    for (std::int64_t y = 0; y < h; ++y) {
        std::uint8_t* row = mask + std::size_t(y) * std::size_t(w);
        std::size_t dp = 0;  // zero-pad default already in place
        while (dp < std::size_t(w)) {
            if (c.pos >= end) return false;
            const int v = static_cast<int8_t>(c.p[c.pos++]);
            if (v == -128) continue;
            if (v >= 0) {
                std::size_t take = std::size_t(v) + 1;
                while (take-- > 0 && dp < std::size_t(w)) {
                    if (c.pos >= end) return false;
                    const std::uint8_t b = c.p[c.pos];
                    c.pos += stride;
                    if (c.pos > end || !c.ok) return false;
                    row[dp++] = b;
                }
            } else {
                if (c.pos + stride > end) return false;
                const std::uint8_t b = c.p[c.pos];
                c.pos += stride;
                std::size_t run = std::size_t(-v) + 1;
                while (run-- > 0 && dp < std::size_t(w)) row[dp++] = b;
            }
        }
    }
    return true;
}

// Parse one GBR at cursor (v1 or v2). `label` synthesizes a fallback name.
LoadedTip parse_gbr(Cursor& c, const std::string& label) {
    LoadedTip out;
    const std::size_t base = c.pos;
    if (!c.need(20)) {
        out.error = "truncated header";
        return out;
    }
    const std::uint32_t headerSize = c.u32be();
    const std::uint32_t version = c.u32be();
    const std::uint32_t w = c.u32be();
    const std::uint32_t h = c.u32be();
    const std::uint32_t bytes = c.u32be();
    std::uint32_t spacing = 25;
    std::size_t nameOff;
    if (version == 1) {
        nameOff = base + 20;
        if (headerSize < 20) {
            out.error = "bad v1 header size";
            return out;
        }
    } else if (version == 2) {
        if (!c.need(8)) {
            out.error = "truncated v2 header";
            return out;
        }
        const std::uint32_t magic = c.u32be();
        spacing = c.u32be();
        if (magic != kTipMagic) {
            out.error = "bad magic";
            return out;
        }
        nameOff = base + 28;
        if (headerSize < 28) {
            out.error = "bad v2 header size";
            return out;
        }
    } else {
        out.error = "unsupported version (want 1/2)";
        return out;
    }
    if (w == 0 || h == 0 || w > kMaxDim || h > kMaxDim) {
        out.error = "bad dimensions";
        return out;
    }
    if (bytes != 1 && bytes != 4) {
        out.error = "unsupported depth (want gray/RGBA)";
        return out;
    }
    if (headerSize > c.n || base + headerSize > c.n) {
        out.error = "header past end";
        return out;
    }
    // Name: bytes up to the header end, cut at the first NUL.
    std::string name;
    if (headerSize > nameOff - base) {
        const std::size_t nl = headerSize - (nameOff - base);
        name.assign(reinterpret_cast<const char*>(c.p + nameOff), nl);
        if (const auto z = name.find('\0'); z != std::string::npos)
            name.erase(z);
    }
    if (name.empty()) name = label;
    const std::size_t bodyOff = base + headerSize;
    const std::size_t bodyLen = std::size_t(w) * h * bytes;
    if (bodyOff + bodyLen > c.n || bodyOff + bodyLen < bodyOff) {
        out.error = "body past end";
        return out;
    }
    StampTip tip;
    tip.w = w;
    tip.h = h;
    tip.spacingPct = std::clamp(float(spacing), 1.0f, 200.0f);
    tip.alpha.assign(std::size_t(w) * h, 0.0f);
    const std::uint8_t* src = c.p + bodyOff;
    if (bytes == 1) {
        for (std::size_t i = 0; i < std::size_t(w) * h; ++i)
            tip.alpha[i] = float(src[i]) / 255.0f;  // mask convention
    } else {
        tip.color = true;
        tip.red.assign(std::size_t(w) * h, 0.0f);
        tip.green.assign(std::size_t(w) * h, 0.0f);
        tip.blue.assign(std::size_t(w) * h, 0.0f);
        for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
            tip.red[i] = float(src[4 * i]) / 255.0f;
            tip.green[i] = float(src[4 * i + 1]) / 255.0f;
            tip.blue[i] = float(src[4 * i + 2]) / 255.0f;
            tip.alpha[i] = float(src[4 * i + 3]) / 255.0f;
        }
    }
    tip.sanitize();
    if (!tip.valid()) {
        out.error = "invalid tip";
        return out;
    }
    c.pos = bodyOff + bodyLen;
    out.tip = std::move(tip);
    out.name = std::move(name);
    out.ok = true;
    return out;
}

// Length-prefixed UTF-16BE brush name (v2 sampled brushes; v1 has none).
// PSD Unicode-string form: u32 character count, then BE code units.
// Strictly validated: any implausible length fails and the caller skips the
// tip, never misaligning the rest of the file. Returns true when the layout
// held (the name itself may be empty); `out` is the UTF-8 name.
bool read_ucs2_name(Cursor& c, std::size_t blockEnd, std::string& out) {
    out.clear();
    // Minimum trailer after the name: antialiasing + bounds + depth +
    // compression flag.
    constexpr std::size_t kTrailer = 1 + 8 + 16 + 2 + 1;
    if (c.pos + 4 > blockEnd || blockEnd - (c.pos + 4) < kTrailer) return false;
    std::uint32_t n = (std::uint32_t(c.p[c.pos]) << 24) |
                      (std::uint32_t(c.p[c.pos + 1]) << 16) |
                      (std::uint32_t(c.p[c.pos + 2]) << 8) |
                      std::uint32_t(c.p[c.pos + 3]);
    if (n > 256) return false;
    if (c.pos + 4 + std::size_t(2) * n + kTrailer > blockEnd + 1) return false;
    const std::uint8_t* q = c.p + c.pos + 4;
    std::string s;
    s.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i, q += 2) {
        const std::uint16_t ch =
            (std::uint16_t(q[0]) << 8) | std::uint16_t(q[1]);
        if (ch == 0) break;  // embedded NUL terminates
        if (ch < 0x80) {
            s.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            s.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else if (ch >= 0xD800 && ch <= 0xDFFF) {
            s.append("\xEF\xBF\xBD");  // lone surrogate -> replacement char
        } else {
            s.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            s.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    c.pos += 4 + std::size_t(2) * n;
    c.ok = c.pos <= blockEnd;
    out = std::move(s);
    return c.ok;
}

// Sampled-brush body of the legacy (.abr v1/v2) layout. Returns false to
// skip (computed/unknown brush kinds, or an unreadable v2 name).
bool parse_abr_sampled_v12(Cursor& c, std::uint32_t blockEnd,
                           const std::string& label, LoadedTip& outTip,
                           bool hasName) {
    const std::size_t start = c.pos;
    if (!c.need(4 + 2)) return false;
    c.skip(4);  // misc
    const std::uint16_t spacing = c.u16be();
    std::string brushName;
    if (hasName && !read_ucs2_name(c, blockEnd, brushName)) {
        c.pos = blockEnd;  // unknown name form: skip the tip, keep the file
        c.ok = true;
        return false;
    }
    if (!c.need(1 + 8 + 16 + 2 + 1)) return false;
    c.skip(1);  // antialiasing
    c.skip(8);  // short bounds
    const std::int32_t top = (std::int32_t(c.u32be()));
    const std::int32_t left = (std::int32_t(c.u32be()));
    const std::int32_t bottom = (std::int32_t(c.u32be()));
    const std::int32_t right = (std::int32_t(c.u32be()));
    const std::uint16_t depth = c.u16be();
    const std::int64_t w = std::int64_t(right) - left;
    const std::int64_t h = std::int64_t(bottom) - top;
    if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim || h > 16384) {
        c.pos = blockEnd;  // wide/degenerate: skip the block, keep parsing
        return false;
    }
    if (depth != 8) {  // 16-bit sampled tips: unsupported for now
        c.pos = blockEnd;
        return false;
    }
    if (!c.need(1)) return false;
    const std::uint8_t compress = c.u8();
    const std::size_t px = std::size_t(w) * std::size_t(h);
    std::vector<std::uint8_t> mask(px, 0);
    if (compress == 0) {
        if (!c.need(px)) return false;
        std::memcpy(mask.data(), c.p + c.pos, px);
        c.pos += px;
    } else {
        // Row-table form first (a u16 compressed length per row, then row
        // PackBits): validate the table against the block before trusting
        // it. Falls back to a plain PackBits stream decode.
        bool tableOk = false;
        const std::size_t dataStart = c.pos;
        if (h <= 4096 && c.pos + std::size_t(2) * std::size_t(h) <= blockEnd) {
            std::size_t sum = 0;
            tableOk = true;
            for (std::int64_t y = 0; y < h; ++y) {
                const std::size_t off = dataStart + std::size_t(2) * std::size_t(y);
                const std::size_t rowLen =
                    (std::size_t(c.p[off]) << 8) | std::size_t(c.p[off + 1]);
                sum += rowLen;
                if (sum > blockEnd - (dataStart + std::size_t(2) * std::size_t(h))) {
                    tableOk = false;
                    break;
                }
            }
            if (tableOk) {
                c.pos = dataStart + std::size_t(2) * std::size_t(h);
                for (std::int64_t y = 0; y < h; ++y) {
                    const std::size_t off =
                        dataStart + std::size_t(2) * std::size_t(y);
                    const std::size_t rowLen =
                        (std::size_t(c.p[off]) << 8) | std::size_t(c.p[off + 1]);
                    const std::size_t rowEnd = c.pos + rowLen;
                    if (rowEnd > blockEnd || rowEnd < c.pos) {
                        tableOk = false;
                        break;
                    }
                    std::uint8_t* row =
                        mask.data() + std::size_t(y) * std::size_t(w);
                    std::size_t dp = 0;
                    while (dp < std::size_t(w)) {
                        if (c.pos >= rowEnd) break;
                        const int n = static_cast<int8_t>(c.p[c.pos++]);
                        if (n == -128) continue;
                        if (n >= 0) {
                            std::size_t take = std::size_t(n) + 1;
                            while (take-- > 0 && dp < std::size_t(w)) {
                                if (c.pos >= rowEnd) break;
                                row[dp++] = c.p[c.pos++];
                            }
                        } else {
                            if (c.pos >= rowEnd) break;
                            const std::uint8_t v = c.p[c.pos++];
                            std::size_t run = std::size_t(-n) + 1;
                            while (run-- > 0 && dp < std::size_t(w))
                                row[dp++] = v;
                        }
                    }
                    c.pos = rowEnd;
                }
            }
        }
        if (!tableOk) {
            // Stream form: plain PackBits for the whole buffer.
            c.pos = dataStart;
            c.ok = true;
            std::fill(mask.begin(), mask.end(), 0);
            if (!stream_decode(c, blockEnd, mask.data(), w, h, 1)) return false;
        }
    }
    StampTip tip;
    tip.w = std::uint32_t(w);
    tip.h = std::uint32_t(h);
    tip.spacingPct = std::clamp(float(spacing), 1.0f, 200.0f);
    tip.alpha.assign(px, 0.0f);
    for (std::size_t i = 0; i < px; ++i)
        tip.alpha[i] = float(mask[i]) / 255.0f;
    tip.sanitize();
    if (!tip.valid()) return false;
    outTip.tip = std::move(tip);
    outTip.name = brushName.empty() ? label : brushName;
    outTip.nameAuthored = !brushName.empty();
    outTip.ok = true;
    if (c.pos > blockEnd || !c.ok) return false;
    c.pos = blockEnd;  // consume any trailing padding
    c.ok = true;
    (void)start;
    return true;
}

}  // namespace

LoadedTip load_gbr(const std::uint8_t* data, std::size_t n) {
    if (!data || n < 20) {
        LoadedTip t;
        t.error = "too small";
        return t;
    }
    Cursor c{data, n, 0};
    return parse_gbr(c, "brush");
}

LoadedHose load_gih(const std::uint8_t* data, std::size_t n) {
    LoadedHose out;
    if (!data || n < 4) {
        out.error = "too small";
        return out;
    }
    // Text header: line 1 = name, line 2 = "<ncells> <params>".
    std::size_t l1 = 0;
    while (l1 < n && data[l1] != '\n') ++l1;
    out.name.assign(reinterpret_cast<const char*>(data), l1);
    if (!out.name.empty() && out.name.back() == '\r') out.name.pop_back();
    std::size_t l2s = l1 < n ? l1 + 1 : n;
    std::size_t l2e = l2s;
    while (l2e < n && data[l2e] != '\n') ++l2e;
    const std::string line2(reinterpret_cast<const char*>(data + l2s),
                            l2e - l2s);
    int ncells = 0;
    {
        std::size_t i = 0;
        while (i < line2.size() && line2[i] != ' ' && line2[i] != '\t') ++i;
        // ncells may lead ("6 ncells:6 ...") or appear as ncells:N.
        ncells = std::atoi(line2.substr(0, i).c_str());
        auto findInt = [&](const char* key) {
            const std::string k(key);
            const auto at = line2.find(k);
            if (at == std::string::npos) return 0;
            return std::atoi(line2.c_str() + at + k.size());
        };
        if (ncells <= 0) ncells = findInt("ncells:");
        const int step = findInt("step:");
        if (step > 0) out.step = std::clamp(step, 1, 200);
        auto findStr = [&](const char* key) {
            const std::string k(key);
            const auto at = line2.find(k);
            if (at == std::string::npos) return std::string();
            const auto s = at + k.size();
            auto e = s;
            while (e < line2.size() && line2[e] != ' ' && line2[e] != '\t')
                ++e;
            return line2.substr(s, e - s);
        };
        out.selection = findStr("selection:");
    }
    if (ncells <= 0 || ncells > 1000) {
        out.error = "bad cell count";
        return out;
    }
    Cursor c{data, n, l2e < n ? l2e + 1 : n};
    for (int i = 0; i < ncells; ++i) {
        if (!c.ok || c.pos >= n) {
            out.error = "truncated cells";
            return out;
        }
        LoadedTip cell =
            parse_gbr(c, out.name + "-" + std::to_string(i));
        if (!cell.ok) {
            out.error = "cell " + std::to_string(i) + ": " + cell.error;
            return out;
        }
        cell.tip.spacingPct = float(out.step);
        out.cells.push_back(std::move(cell));
        if (out.cells.size() > kMaxTips) {
            out.error = "too many cells";
            return out;
        }
    }
    out.ok = true;
    return out;
}

LoadedAbr load_abr(const std::uint8_t* data, std::size_t n) {
    LoadedAbr out;
    if (!data || n < 4) {
        out.error = "too small";
        return out;
    }
    Cursor c{data, n, 0};
    const std::uint16_t version = c.u16be();
    const std::uint16_t count = c.u16be();
    if (!c.ok) {
        out.error = "truncated header";
        return out;
    }
    if (version == 1 || version == 2) {
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!c.need(6)) {
                out.error = "truncated brush entry";
                return out;
            }
            const std::uint16_t type = c.u16be();
            const std::uint32_t size = c.u32be();
            if (size > n) {
                out.error = "bad block size";
                return out;
            }
            const std::size_t blockEnd = c.pos + size;
            if (blockEnd > n || blockEnd < c.pos) {
                out.error = "block past end";
                return out;
            }
            if (type == 2) {
                LoadedTip tip;
                if (parse_abr_sampled_v12(
                        c, blockEnd, "tip-" + std::to_string(i), tip,
                        version == 2)) {
                    out.tips.push_back(std::move(tip));
                    if (out.tips.size() > kMaxTips) {
                        out.error = "too many tips";
                        return out;
                    }
                } else if (!c.ok) {
                    out.error = "corrupt sampled brush";
                    return out;
                } else {
                    c.pos = blockEnd;  // skipped degenerate block
                }
            } else {
                // Computed (type 1) tips are procedural: the descriptor
                // binary layout is not publicly documented, so sampling
                // them would mean guessing at offsets. Count them for an
                // honest import note (auto-tip approximation) instead.
                if (type == 1) ++out.skippedComputed;
                c.pos = blockEnd;  // computed (type 1) + unknown: skip
            }
        }
        out.ok = true;
        return out;
    }
    if (version == 6 || version == 7 || version == 9 || version == 10) {
        // Tagged-block layout: 4-byte signature + 4-byte key + u32 length +
        // payload (padded to a multiple of 4). The header's second u16 is
        // the layout sub-version here. Unknown blocks and unknown item
        // variants are skipped by length.
        const std::uint16_t subversion = count;
        int serial = 0;
        while (c.pos + 12 <= n) {
            if (std::memcmp(c.p + c.pos, "8BIM", 4) != 0) break;
            const std::string key(reinterpret_cast<const char*>(c.p + c.pos + 4),
                                  4);
            c.pos += 8;
            const std::uint32_t len = c.u32be();
            if (!c.ok || len > n) {
                out.error = "bad block length";
                return out;
            }
            const std::size_t payEnd = c.pos + len;
            if (payEnd > n || payEnd < c.pos) {
                out.error = "block past end";
                return out;
            }
            if (key == "samp") {
                // Series of length-prefixed items; unknown variants skip.
                while (c.pos + 4 <= payEnd) {
                    const std::uint32_t itemLen = c.u32be();
                    if (!c.ok) break;
                    const std::size_t itemEnd = c.pos + itemLen;
                    if (itemLen > payEnd || itemEnd > payEnd ||
                        itemEnd < c.pos) {
                        c.ok = false;
                        break;
                    }
                    // Sampled-item layout: u32 length already consumed, then
                    // a 37-byte key, a version-dependent unknown run (10 or
                    // 264 bytes), the tip rectangle, bit depth, compression
                    // flag, then the pixels. Anything that fails validation
                    // is skipped by length (covers newer item variants).
                    bool parsed = false;
                    const std::size_t headSkip =
                        37 + (subversion == 1 ? 10 : 264);
                    if (itemEnd - c.pos >= headSkip + 16 + 2 + 1) {
                        c.skip(headSkip);
                        const std::int32_t top = std::int32_t(c.u32be());
                        const std::int32_t left = std::int32_t(c.u32be());
                        const std::int32_t bottom = std::int32_t(c.u32be());
                        const std::int32_t right = std::int32_t(c.u32be());
                        const std::uint16_t depth = c.u16be();
                        const std::int64_t w =
                            std::int64_t(right) - left;
                        const std::int64_t h =
                            std::int64_t(bottom) - top;
                        if (c.ok && w > 0 && h > 0 && w <= kMaxDim &&
                            h <= kMaxDim && (depth == 8 || depth == 16)) {
                            if (c.pos < itemEnd) {
                                const std::uint8_t compress = c.p[c.pos++];
                                c.ok = c.pos <= itemEnd;
                                const std::size_t px =
                                    std::size_t(w) * std::size_t(h);
                                const std::size_t stride =
                                    depth == 16 ? 2 : 1;
                                std::vector<std::uint8_t> mask(px, 0);
                                if (compress == 0) {
                                    if (c.pos + px * stride <= itemEnd) {
                                        for (std::size_t i = 0; i < px; ++i)
                                            mask[i] =
                                                c.p[c.pos + i * stride];
                                        c.pos += px * stride;
                                        parsed = true;
                                    }
                                } else if (stream_decode(c, itemEnd,
                                                         mask.data(), w, h,
                                                         stride)) {
                                    parsed = true;
                                }
                                if (parsed) {
                                    StampTip tip;
                                    tip.w = std::uint32_t(w);
                                    tip.h = std::uint32_t(h);
                                    tip.spacingPct = 25.0f;  // per-dab
                                                             // dynamics are a
                                                             // later slice
                                    tip.alpha.assign(px, 0.0f);
                                    for (std::size_t i = 0; i < px; ++i)
                                        tip.alpha[i] =
                                            float(mask[i]) / 255.0f;
                                    tip.sanitize();
                                    LoadedTip lt;
                                    lt.tip = std::move(tip);
                                    lt.name =
                                        "tip-" + std::to_string(serial++);
                                    lt.ok = true;
                                    if (lt.tip.valid()) {
                                        out.tips.push_back(std::move(lt));
                                        if (out.tips.size() > kMaxTips) {
                                            out.error = "too many tips";
                                            return out;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    c.pos = itemEnd;  // skip unknown variants / leftovers
                    c.ok = true;
                }
            }
            c.pos = payEnd + ((payEnd % 4 == 0) ? 0 : 4 - payEnd % 4);
            c.ok = true;
        }
        out.ok = true;
        return out;
    }
    out.error = "unsupported brush-file version";
    return out;
}

std::size_t hose_cell_index(const LoadedHose& hose, std::size_t dabNumber,
                            std::uint64_t seed, double directionRad,
                            double pressure, double speed01) {
    if (hose.cells.empty()) return 0;
    if (hose.selection == "random") {
        // SplitMix64, seeded per stroke + dab: deterministic per stroke.
        std::uint64_t z = seed + dabNumber * 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= (z >> 31);
        return std::size_t(z % hose.cells.size());
    }
    if (hose.selection == "angular") {
        const double twoPi = 2.0 * 3.141592653589793;
        double a = std::fmod(directionRad, twoPi);
        if (a < 0.0) a += twoPi;
        return std::size_t(a / twoPi * hose.cells.size()) % hose.cells.size();
    }
    if (hose.selection == "velocity") {
        const double s = std::clamp(speed01, 0.0, 1.0);
        return std::min(std::size_t(s * hose.cells.size()),
                        hose.cells.size() - 1);
    }
    if (hose.selection == "pressure") {
        const double p = std::clamp(pressure, 0.0, 1.0);
        return std::min(std::size_t(p * hose.cells.size()),
                        hose.cells.size() - 1);
    }
    return dabNumber % hose.cells.size();  // incremental + fallback
}

}  // namespace pittore::compute::brushload
