// Container-level embedded ICC extraction: JPEG APP2 / PNG iCCP / WebP ICCP.
// See icc_scan.h for why the profile is read out of the container rather than
// taken from the decoded image's color space.

#include "icc_scan.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>

#include <zlib.h>

#include "icc.h"

namespace pittore::io {
namespace {

// Generous but bounded: the largest legitimate profiles (device-link /
// spectral) sit well under 10 MB, and a hostile file must not be able to ask
// for an unbounded allocation.
constexpr std::size_t kMaxProfile = 64u * 1024u * 1024u;

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

// Inflate a zlib stream of unknown length (iCCP stores no uncompressed size)
// into a bounded buffer.
bool inflateBounded(const std::uint8_t* src, std::size_t n,
                    std::vector<std::uint8_t>* out) {
    if (n < 6 || n > kMaxProfile) return false;  // zlib header at minimum
    z_stream zs;
    std::memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(src);
    // uInt is 32-bit; a profile chunk never comes near it, but never let the
    // cast wrap either.
    zs.avail_in = static_cast<uInt>(std::min<std::size_t>(n, 0xFFFFFFF0u));

    std::vector<std::uint8_t> buf;
    buf.resize(n * 4 + 4096);
    std::size_t produced = 0;
    bool done = false;
    bool ok = false;
    for (;;) {
        if (produced == buf.size()) {
            if (buf.size() >= kMaxProfile) break;
            buf.resize(std::min(kMaxProfile, buf.size() * 2));
        }
        zs.next_out = buf.data() + produced;
        zs.avail_out = static_cast<uInt>(buf.size() - produced);
        const int rc = inflate(&zs, Z_NO_FLUSH);
        produced = buf.size() - zs.avail_out;
        if (rc == Z_STREAM_END) {
            ok = true;
            done = true;
            break;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR) break;
        if (zs.avail_in == 0 && zs.avail_out != 0) break;  // truncated input
    }
    inflateEnd(&zs);
    if (!done) return false;
    out->assign(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(produced));
    return true;
}

// JPEG: walk markers to the APP2 segments tagged "ICC_PROFILE" and splice
// their numbered parts back together.
bool jpegIcc(const std::uint8_t* p, std::size_t n,
             std::vector<std::uint8_t>* out) {
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;
    int total = 0;
    std::vector<std::pair<const std::uint8_t*, std::size_t>> parts;  // 1-based
    parts.emplace_back(nullptr, 0);
    std::size_t i = 2;
    while (i + 4 <= n) {
        if (p[i] != 0xFF) break;  // entropy/stray bytes: no more segments
        const std::uint8_t marker = p[i + 1];
        if (marker == 0xFF) { ++i; continue; }  // fill byte
        if (marker == 0x00) { i += 2; continue; }
        // Standalone markers carry no length field.
        if (marker == 0x01 || marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        if (marker == 0xD9) break;  // EOI
        const std::size_t len =
            (static_cast<std::size_t>(p[i + 2]) << 8) | p[i + 3];
        if (len < 2) return false;
        const std::size_t data = i + 4;
        const std::size_t dataLen = len - 2;
        if (data > n || dataLen > n - data) return false;  // truncated
        if (marker == 0xDA) break;  // SOS: scan data follows, no APP after it
        if (marker == 0xE2 && dataLen >= 14 &&
            std::memcmp(p + data, "ICC_PROFILE\0", 12) == 0) {
            const int seq = p[data + 12];
            const int count = p[data + 13];
            if (count >= 1 && count <= 255 && seq >= 1 && seq <= count) {
                if (total == 0) {
                    total = count;
                    parts.assign(static_cast<std::size_t>(count) + 1,
                                 std::pair<const std::uint8_t*, std::size_t>(
                                     nullptr, 0));
                }
                if (count == total)
                    parts[static_cast<std::size_t>(seq)] =
                        std::make_pair(p + data + 14, dataLen - 14);
            }
        }
        i = data + dataLen;
    }
    if (total <= 0) return false;
    std::size_t bytes = 0;
    for (int s = 1; s <= total; ++s) {
        if (parts[static_cast<std::size_t>(s)].first == nullptr) return false;
        bytes += parts[static_cast<std::size_t>(s)].second;
    }
    if (bytes == 0 || bytes > kMaxProfile) return false;
    out->clear();
    out->reserve(bytes);
    for (int s = 1; s <= total; ++s) {
        const auto& part = parts[static_cast<std::size_t>(s)];
        out->insert(out->end(), part.first, part.first + part.second);
    }
    return true;
}

// PNG: iCCP must precede PLTE/IDAT, so the chunk walk stops at the image data.
bool pngIcc(const std::uint8_t* p, std::size_t n,
            std::vector<std::uint8_t>* out) {
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G',
                                        0x0D, 0x0A, 0x1A, 0x0A};
    if (n < 8 || std::memcmp(p, sig, 8) != 0) return false;
    std::size_t i = 8;
    while (i + 12 <= n) {
        const std::uint32_t len = be32(p + i);
        const std::uint8_t* type = p + i + 4;
        if (static_cast<std::size_t>(len) > n - i - 12) return false;
        const std::uint8_t* data = p + i + 8;
        if (std::memcmp(type, "iCCP", 4) == 0) {
            std::size_t k = 0;
            while (k < len && data[k] != 0) ++k;  // profile name
            if (k == 0 || k >= 80 || k + 2 > len) return false;
            if (data[k + 1] != 0) return false;  // compression method: deflate
            return inflateBounded(data + k + 2, len - (k + 2), out);
        }
        if (std::memcmp(type, "IDAT", 4) == 0 ||
            std::memcmp(type, "PLTE", 4) == 0 ||
            std::memcmp(type, "IEND", 4) == 0)
            return false;  // iCCP would have come first
        i += 12 + len;
    }
    return false;
}

// WebP: VP8X advertises the ICCP chunk that follows it.
bool webpIcc(const std::uint8_t* p, std::size_t n,
             std::vector<std::uint8_t>* out) {
    if (n < 16 || std::memcmp(p, "RIFF", 4) != 0 ||
        std::memcmp(p + 8, "WEBP", 4) != 0)
        return false;
    bool wantIcc = false;
    std::size_t i = 12;
    while (i + 8 <= n) {
        const std::uint32_t len = le32(p + i + 4);
        const std::size_t data = i + 8;
        if (static_cast<std::size_t>(len) > n - data) return false;
        if (std::memcmp(p + i, "VP8X", 4) == 0 && len >= 1)
            wantIcc = (p[data] & 0x20) != 0;
        if (std::memcmp(p + i, "ICCP", 4) == 0) {
            if (!wantIcc || len == 0 || len > kMaxProfile) return false;
            out->assign(p + data, p + data + len);
            return true;
        }
        i = data + len + (len & 1u);  // chunks pad to even size
    }
    return false;
}

}  // namespace

bool containerIccProfile(const std::uint8_t* p, std::size_t n,
                         std::vector<std::uint8_t>* out) {
    if (!p || !out || n < 12) return false;
    if (p[0] == 0xFF && p[1] == 0xD8) return jpegIcc(p, n, out);
    if (p[0] == 0x89 && p[1] == 'P') return pngIcc(p, n, out);
    if (p[0] == 'R' && p[1] == 'I') return webpIcc(p, n, out);
    return false;
}

std::string containerIccProfileName(const char* path) {
    if (!path) return "";
    // 8 MB covers the spec-placed profile in every realistic file (JPEG keeps
    // its APP2 segments next to SOI; PNG requires iCCP before IDAT), while
    // keeping the scan a bounded read even for a panorama.
    constexpr std::size_t kHeadBytes = 8u * 1024u * 1024u;
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::vector<std::uint8_t> buf(kHeadBytes);
    f.read(reinterpret_cast<char*>(buf.data()),
           static_cast<std::streamsize>(buf.size()));
    const std::size_t got = static_cast<std::size_t>(f.gcount());
    if (got == 0) return "";
    std::vector<std::uint8_t> icc;
    if (!containerIccProfile(buf.data(), got, &icc)) return "";
    return iccProfileDescription(icc.data(), icc.size());
}

}  // namespace pittore::io
