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


// ---------------------------------------------------------------------------
// Metadata object-tree scan (PITTORE_AF). Names are stored as the reversed
// 4-char identifier ("WpmB" = BmpW, "nveR" = Revision, "NFRI" = IRFN link).
// ---------------------------------------------------------------------------

// True when a recorded string looks like a placed-resource file name rather
// than an internal block reference (the .af format addresses data blocks as
// "d/<hex>" / "b/<hex>"). Real thumbnails are usually bare file names with
// no directory part, so the separator is not required.
bool looksLikeResourcePath(const std::string& s) {
    if (s.size() < 5 || s.size() > 4096) return false;
    const std::size_t last = s.find_last_of("/\\");
    const std::string base = last == std::string::npos ? s : s.substr(last + 1);
    if (base.empty()) return false;
    const bool hexOnly = base.size() >= 2 && base.size() <= 10 &&
                         std::all_of(base.begin(), base.end(), [](char c) {
                             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                                    (c >= 'A' && c <= 'F');
                         });
    return !hexOnly;
}


// Scans one decompressed metadata tree for the version atoms and the linked
// resource (placement) records, which are grouped per resource as
//   IPDO(0x07 dpi) … NFRI(0x2b path) … WpmB(0x07) HpmB(0x07)
void scanMetaTree(const std::vector<std::uint8_t>& b, AfDocument& doc) {
    std::size_t lastNfri = 0;
    std::size_t lastIpd = 0;
    std::uint32_t ipdValue = 0;
    for (std::size_t i = 4; i + 12 <= b.size(); ++i) {
        const std::uint8_t* p = b.data() + i;
        const std::uint8_t tag = p[-1];
        const bool isStr = tag == 0x2b || tag == 0x33;
        if (p[0] == 'N' && p[1] == 'F' && p[2] == 'R' && p[3] == 'I' && isStr) {
            const std::uint32_t len = u32le(p + 4);
            if (len > 0 && len <= 4096 && i + 8 + len <= b.size()) {
                bool printable = true;
                for (std::uint32_t k = 0; k < len; ++k)
                    if (b[i + 8 + k] < 0x20 || b[i + 8 + k] > 0x7e) printable = false;
                if (printable) {
                    const std::string path(reinterpret_cast<const char*>(p + 8), len);
                    if (looksLikeResourcePath(path) && doc.placements.size() < 256) {
                        AfPlacement pl;
                        pl.path = path;
                        if (lastIpd != 0 && i - lastIpd <= 1024) pl.dpi = ipdValue;
                        doc.placements.push_back(std::move(pl));
                        lastNfri = i;
                        lastIpd = 0;
                    }
                }
            }
            continue;
        }
        if (tag != 0x07) continue;
        if (p[0] == 'W' && p[1] == 'p' && p[2] == 'm' && p[3] == 'B') {
            if (!doc.placements.empty() && lastNfri != 0 && i - lastNfri <= 4096 &&
                doc.placements.back().width == 0)
                doc.placements.back().width = u32le(p + 4);
        } else if (p[0] == 'H' && p[1] == 'p' && p[2] == 'm' && p[3] == 'B') {
            if (!doc.placements.empty() && lastNfri != 0 && i - lastNfri <= 4096 &&
                doc.placements.back().height == 0)
                doc.placements.back().height = u32le(p + 4);
        } else if (p[0] == 'I' && p[1] == 'P' && p[2] == 'D' && p[3] == 'O') {
            lastIpd = i;
            ipdValue = u32le(p + 4);
        } else if (p[0] == 'n' && p[1] == 'v' && p[2] == 'e' && p[3] == 'R') {
            doc.revision = std::max(doc.revision, u32le(p + 4));
        } else if (p[0] == 'r' && p[1] == 'j' && p[2] == 'a' && p[3] == 'M') {
            doc.appVersion = std::to_string(u32le(p + 4));
        } else if (p[0] == 'r' && p[1] == 'n' && p[2] == 'i' && p[3] == 'M') {
            doc.appVersion += "." + std::to_string(u32le(p + 4));
        } else if (p[0] == 'd' && p[1] == 'l' && p[2] == 'i' && p[3] == 'B') {
            doc.appVersion += "." + std::to_string(u32le(p + 4));
        }
    }
}

// Decompress a single self-delimiting zstd frame. ZSTD_decompressBound is a
// static-only symbol (ZSTDLIB_STATIC_API) that the shared libzstd does not
// export, so the stable public API is used instead: the exact size from the
// frame header when present, otherwise the streaming API with a growing
// output buffer (capped at kMaxDecompressed).
#ifdef PITTORE_AF
bool zstdFrameDecompress(const std::uint8_t* src, std::size_t srcSize,
                         std::vector<std::uint8_t>& out) {
    const unsigned long long known = ZSTD_getFrameContentSize(src, srcSize);
    out.clear();
    // The frame header may omit the content size entirely (the format's tiles
    // do), in which case the size query errors out; only the in-cap one-shot
    // path is used when a size is signalled, everything else falls through to
    // the streaming decoder below.
    if (!ZSTD_isError(known) && known != ZSTD_CONTENTSIZE_UNKNOWN && known != 0 &&
        known <= kMaxDecompressed) {
        out.resize(static_cast<std::size_t>(known));
        const std::size_t got = ZSTD_decompress(out.data(), out.size(), src, srcSize);
        if (!ZSTD_isError(got)) {
            out.resize(got);
            return true;
        }
        out.clear();
    }
    ZSTD_DCtx* ctx = ZSTD_createDCtx();
    if (!ctx) return false;
    ZSTD_inBuffer in{src, srcSize, 0};
    bool ok = false;
    while (out.size() <= kMaxDecompressed) {
        const std::size_t chunk = 1u << 20;
        const std::size_t old = out.size();
        out.resize(old + chunk);
        ZSTD_outBuffer ob{out.data() + old, chunk, 0};
        const std::size_t rem = ZSTD_decompressStream(ctx, &ob, &in);
        out.resize(old + ob.pos);
        if (ZSTD_isError(rem)) break;
        if (rem == 0) {
            ok = true;
            break;
        }
    }
    ZSTD_freeDCtx(ctx);
    return ok;
}
#endif


void walkFrames(const std::vector<std::uint8_t>& b, AfDocument& doc) {
#ifdef PITTORE_AF
    std::size_t pos = 0;
    for (; pos + 12 <= b.size() && pos < 8192; ++pos) {
        if (hasBytes(b, reinterpret_cast<const std::uint8_t*>("#Fil"), 4, pos) &&
            hasBytes(b, kZstdMagic, 4, pos + 4))
            break;
    }
    if (pos + 12 > b.size()) return;
    std::uint64_t decompressedTotal = 0;
    int metaScans = 0;
    while (pos + 12 <= b.size()) {
        if (!hasBytes(b, reinterpret_cast<const std::uint8_t*>("#Fil"), 4, pos) ||
            !hasBytes(b, kZstdMagic, 4, pos + 4))
            break;  // cleartext tail (PNG previews / Meta / JSON)
        const std::size_t payload = pos + 4;
        const std::size_t avail = b.size() - payload;
        const std::size_t compSize = ZSTD_findFrameCompressedSize(b.data() + payload, avail);
        if (ZSTD_isError(compSize) || compSize > avail) break;
        std::vector<std::uint8_t> out;
        if (!zstdFrameDecompress(b.data() + payload, avail, out)) break;
        const std::size_t got = out.size();
        if (got == 0) break;
        ++doc.frameCount;
        decompressedTotal += got;
        if (got > kMetaMinSize && metaScans < 16 && got <= (1u << 22)) {
            scanMetaTree(out, doc);
            ++metaScans;
        }
        if (decompressedTotal > kMaxDecompressed) break;
        pos = payload + compSize + 4;  // + FF FF FF FF trailer
    }
#else
    static_cast<void>(b);
    static_cast<void>(doc);
#endif
}

}  // namespace af_preview
}  // namespace pittore::io
