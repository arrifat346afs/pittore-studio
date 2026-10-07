#include "engine/io/af.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#include <zlib.h>
#ifdef PITTORE_AF
// ZSTD_decompressBound lives in zstd's advanced API; the header only declares
// it when this macro is set first (see zstd.h). The symbol itself is exported
// by the shared library, so no static-link constraint is introduced.
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#endif

#include "engine/io/af/shared/af_preview.h"
#include "engine/io/af/png/af_png.h"

namespace pittore::io {
namespace af_preview {


bool inflateIdat(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& out,
                 std::string* why) {
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) {
        *why = "PNG: inflate init failed";
        return false;
    }
    zs.next_in = const_cast<Bytef*>(src.data());
    zs.avail_in = static_cast<uInt>(src.size());
    std::vector<std::uint8_t> buf(1u << 16);
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        zs.next_out = buf.data();
        zs.avail_out = static_cast<uInt>(buf.size());
        const std::size_t before = zs.total_out;
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&zs);
            *why = "PNG: corrupt IDAT stream";
            return false;
        }
        if (ret == Z_BUF_ERROR && zs.avail_out == buf.size()) {
            inflateEnd(&zs);
            *why = "PNG: IDAT stream made no progress";
            return false;
        }
        out.insert(out.end(), buf.begin(), buf.begin() + (buf.size() - zs.avail_out));
        if (out.size() > kMaxDecompressed) {
            inflateEnd(&zs);
            *why = "PNG: IDAT stream too large";
            return false;
        }
        static_cast<void>(before);
    }
    inflateEnd(&zs);
    return true;
}


// Parses the PNG at `start` (right after the 8-byte signature). On success
// `*endOut` is one byte past the IEND chunk and the scanline data is inflated.
bool parsePng(const std::vector<std::uint8_t>& b, std::size_t start, PngInfo& png,
              std::size_t* endOut, std::string* why) {
    if (start + 8 + 8 > b.size()) {
        *why = "PNG: truncated header";
        return false;
    }
    std::size_t p = start + 8;
    const std::size_t len = u32be(b.data() + p);
    p += 4;
    if (len != 13 || !hasBytes(b, reinterpret_cast<const std::uint8_t*>("IHDR"), 4, p)) {
        *why = "PNG: missing IHDR";
        return false;
    }
    const std::uint8_t* ihdr = b.data() + p + 4;
    png.width = u32be(ihdr);
    png.height = u32be(ihdr + 4);
    png.bitDepth = ihdr[8];
    png.colorType = ihdr[9];
    const int method = ihdr[10];
    const int filterMethod = ihdr[11];
    png.interlace = ihdr[12];
    if (png.width == 0 || png.height == 0 || png.width > kMaxPngDimension ||
        png.height > kMaxPngDimension ||
        std::uint64_t(png.width) * png.height * 4 > kMaxPixels) {
        *why = "PNG: bad dimensions";
        return false;
    }
    if (method != 0 || filterMethod != 0 || (png.interlace != 0 && png.interlace != 1)) {
        *why = "PNG: unsupported compression/filter/interlace method";
        return false;
    }
    const auto validDepth = [](int d, int ct) {
        switch (ct) {
            case 0: return d == 1 || d == 2 || d == 4 || d == 8 || d == 16;
            case 2: return d == 8 || d == 16;
            case 3: return d == 1 || d == 2 || d == 4 || d == 8;
            case 4: return d == 8 || d == 16;
            case 6: return d == 8 || d == 16;
            default: return false;
        }
    };
    if (!validDepth(png.bitDepth, png.colorType)) {
        *why = "PNG: unsupported bit depth/color type";
        return false;
    }
    p += 4 + len + 4;  // IHDR data + CRC

    std::vector<std::uint8_t> idat;
    bool haveIend = false;
    while (p + 12 <= b.size()) {
        const std::size_t clen = u32be(b.data() + p);
        if (p + 12 + clen > b.size()) {
            *why = "PNG: truncated chunk";
            return false;
        }
        const std::uint32_t storedCrc = u32be(b.data() + p + 8 + clen);
        const std::uint32_t wantCrc =
            crc32(0, b.data() + p + 4, static_cast<uInt>(4 + clen));
        if (storedCrc != wantCrc) {
            char typ[5];
            std::memcpy(typ, b.data() + p + 4, 4);
            typ[4] = 0;
            char dbg[128];
            std::snprintf(dbg, sizeof(dbg), "PNG: bad chunk CRC %s@%zu stored=%08x want=%08x",
                          typ, p, storedCrc, wantCrc);
            *why = dbg;
            return false;
        }
        const std::uint8_t* type = b.data() + p + 4;
        if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), b.begin() + p + 8, b.begin() + p + 8 + clen);
            if (idat.size() > kMaxDecompressed) {
                *why = "PNG: IDAT too large";
                return false;
            }
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            png.palette.assign(b.begin() + p + 8, b.begin() + p + 8 + clen);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            png.trns.assign(b.begin() + p + 8, b.begin() + p + 8 + clen);
        } else if (std::memcmp(type, "pHYs", 4) == 0 && clen >= 9) {
            const std::uint8_t* q = b.data() + p + 8;
            png.dpmX = u32be(q);
            png.dpmY = u32be(q + 4);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            haveIend = true;
            p += 12 + clen;
            break;
        }
        p += 12 + clen;
    }
    if (!haveIend) {
        *why = "PNG: no IEND";
        return false;
    }
    if (idat.empty()) {
        *why = "PNG: no IDAT";
        return false;
    }
    if (!inflateIdat(idat, png.inflated, why)) return false;
    *endOut = p;
    return true;
}


int channelsOf(int colorType) {
    switch (colorType) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}


// Bit replication of a <8-bit sample into a full byte (PNG-spec scaling).
std::uint8_t rep8(std::uint32_t v, int depth) {
    std::uint8_t out = 0;
    for (int shift = 8 - depth; shift >= 0; shift -= depth)
        out = static_cast<std::uint8_t>(out | ((v & ((1u << depth) - 1)) << shift));
    return out;
}


std::uint32_t scale16(std::uint32_t v, int depth) {
    if (depth == 16) return v;
    if (depth == 8) return v * 257;
    return std::uint32_t(rep8(v, depth)) * 257;
}


void readSamples(const std::uint8_t* row, std::size_t px, int bitDepth, int channels,
                 std::uint32_t* s) {
    if (bitDepth == 8) {
        const std::uint8_t* p = row + px * std::size_t(channels);
        for (int c = 0; c < channels; ++c) s[c] = p[c];
    } else if (bitDepth == 16) {
        const std::uint8_t* p = row + px * std::size_t(channels) * 2;
        for (int c = 0; c < channels; ++c)
            s[c] = (std::uint32_t(p[c * 2]) << 8) | p[c * 2 + 1];
    } else {
        const std::uint32_t mask = (1u << bitDepth) - 1;
        for (int c = 0; c < channels; ++c) {
            const std::size_t bit = px * std::size_t(bitDepth) + c * std::size_t(bitDepth);
            const int shift = 8 - bitDepth - static_cast<int>(bit & 7);
            s[c] = (row[bit >> 3] >> shift) & mask;
        }
    }
}


void expandPixel(std::uint32_t* s, const PngInfo& png, std::uint16_t rgba4[4]) {
    const int d = png.bitDepth;
    const auto keyAt = [&](std::size_t ci) {
        if (ci * 2 + 1 >= png.trns.size()) return std::uint32_t(0xffffffffu);
        const std::uint32_t key = (std::uint32_t(png.trns[ci * 2]) << 8) | png.trns[ci * 2 + 1];
        return d == 16 ? key : key >> (16 - d);
    };
    std::uint32_t r = 0, g = 0, b = 0;
    std::uint16_t a = 65535;
    switch (png.colorType) {
        case 0:
            r = g = b = scale16(s[0], d);
            if (!png.trns.empty() && s[0] == keyAt(0)) a = 0;
            break;
        case 2:
            r = scale16(s[0], d);
            g = scale16(s[1], d);
            b = scale16(s[2], d);
            if (png.trns.size() >= 6 && s[0] == keyAt(0) && s[1] == keyAt(1) &&
                s[2] == keyAt(2))
                a = 0;
            break;
        case 3: {
            const std::uint32_t idx = s[0];
            if (idx * 3 + 2 < png.palette.size()) {
                r = scale16(png.palette[idx * 3], 8);
                g = scale16(png.palette[idx * 3 + 1], 8);
                b = scale16(png.palette[idx * 3 + 2], 8);
            }
            if (idx < png.trns.size()) a = std::uint16_t(png.trns[idx]) * 257;
            break;
        }
        case 4:
            r = g = b = scale16(s[0], d);
            a = std::uint16_t(scale16(s[1], d));
            if (!png.trns.empty() && s[0] == keyAt(0)) a = 0;
            break;
        case 6:
            r = scale16(s[0], d);
            g = scale16(s[1], d);
            b = scale16(s[2], d);
            a = std::uint16_t(scale16(s[3], d));
            break;
        default: break;
    }
    rgba4[0] = std::uint16_t(r);
    rgba4[1] = std::uint16_t(g);
    rgba4[2] = std::uint16_t(b);
    rgba4[3] = a;
}


// Reverses one PNG filter row in place (row/prev include the filter byte).
bool unfilterRow(std::uint8_t* row, const std::uint8_t* prev, std::size_t len,
                 std::size_t bppBytes) {
    const std::uint8_t f = row[0];
    std::uint8_t* d = row + 1;
    const std::uint8_t* p = prev + 1;
    switch (f) {
        case 0: return true;
        case 1:
            for (std::size_t i = bppBytes; i < len; ++i)
                d[i] = static_cast<std::uint8_t>(d[i] + d[i - bppBytes]);
            return true;
        case 2:
            for (std::size_t i = 0; i < len; ++i)
                d[i] = static_cast<std::uint8_t>(d[i] + p[i]);
            return true;
        case 3:
            for (std::size_t i = 0; i < len; ++i) {
                const int left = i >= bppBytes ? d[i - bppBytes] : 0;
                d[i] = static_cast<std::uint8_t>(d[i] + ((left + p[i]) >> 1));
            }
            return true;
        case 4:
            for (std::size_t i = 0; i < len; ++i) {
                const int left = i >= bppBytes ? d[i - bppBytes] : 0;
                const int up = p[i];
                const int ul = i >= bppBytes ? p[i - bppBytes] : 0;
                const int pp = left + up - ul;
                const int pa = std::abs(pp - left), pb = std::abs(pp - up),
                          pc = std::abs(pp - ul);
                const int pr = (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : ul);
                d[i] = static_cast<std::uint8_t>(d[i] + pr);
            }
            return true;
        default: return false;
    }
}


std::size_t passSize(std::uint32_t total, std::uint32_t start, std::uint32_t step) {
    return total > start ? (std::size_t(total) - 1 - start) / step + 1 : 0;
}


bool decodePass(const PngInfo& png, std::size_t dataPos, std::size_t pw, std::size_t ph,
                std::size_t rowBytes, std::size_t bppBytes, const PixelPass& pass,
                std::vector<std::uint16_t>& rgba) {
    if (dataPos + ph * (1 + rowBytes) > png.inflated.size()) return false;
    std::vector<std::uint8_t> cur(rowBytes + 1, 0), prev(rowBytes + 1, 0);
    const int channels = channelsOf(png.colorType);
    for (std::size_t py = 0; py < ph; ++py) {
        const std::uint8_t* src = png.inflated.data() + dataPos + py * (1 + rowBytes);
        std::copy(src, src + rowBytes + 1, cur.begin());
        if (!unfilterRow(cur.data(), prev.data(), rowBytes, bppBytes)) return false;
        const std::uint8_t* row = cur.data() + 1;
        for (std::size_t px = 0; px < pw; ++px) {
            std::uint32_t s[4] = {0, 0, 0, 0};
            readSamples(row, px, png.bitDepth, channels, s);
            std::uint16_t c[4];
            expandPixel(s, png, c);
            const std::size_t x = pass.x0 + px * pass.dx;
            const std::size_t y = pass.y0 + py * pass.dy;
            std::uint16_t* dst = rgba.data() + (y * png.width + x) * 4;
            for (int k = 0; k < 4; ++k) dst[k] = c[k];
        }
        std::swap(cur, prev);
    }
    return true;
}


bool decodePngToRgba16(const PngInfo& png, std::vector<std::uint16_t>& rgba,
                       std::string* why) {
    rgba.assign(std::size_t(png.width) * png.height * 4, 0);
    const int channels = channelsOf(png.colorType);
    if (channels == 0) {
        *why = "PNG: unsupported color type";
        return false;
    }
    const std::size_t bppBytes =
        std::max<std::size_t>(1, std::size_t(channels * png.bitDepth) / 8);

    if (png.interlace == 1) {
        std::size_t pos = 0;
        for (int p = 0; p < 7; ++p) {
            const std::size_t pw = passSize(png.width, kAdam7[p].x0, kAdam7[p].dx);
            const std::size_t ph = passSize(png.height, kAdam7[p].y0, kAdam7[p].dy);
            if (pw == 0 || ph == 0) continue;
            const std::size_t rowBytes = (pw * std::size_t(png.bitDepth) * channels + 7) / 8;
            if (!decodePass(png, pos, pw, ph, rowBytes, bppBytes, kAdam7[p], rgba)) {
                *why = "PNG: interlaced scanline data truncated";
                return false;
            }
            pos += ph * (1 + rowBytes);
        }
    } else {
        const std::size_t rowBytes =
            (std::size_t(png.width) * png.bitDepth * channels + 7) / 8;
        const PixelPass single = {0, 0, 1, 1};
        if (!decodePass(png, 0, png.width, png.height, rowBytes, bppBytes, single, rgba)) {
            *why = "PNG: scanline data truncated";
            return false;
        }
    }
    return true;
}

}  // namespace af_preview
}  // namespace pittore::io
