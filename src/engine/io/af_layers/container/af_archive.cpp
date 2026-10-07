#include "engine/io/af_layers.h"
#include "engine/io/af.h"
#include "engine/render/layer_style.h"
#include "engine/text/text_engine.h"
#include "engine/vector/vector_shape.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>
#ifdef PITTORE_AF
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#endif
#ifdef PITTORE_WEBP
#include "engine/io/webp.h"
#endif
#ifdef PITTORE_JPEG
#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>
#endif
#include "engine/io/af_layers/container/af_archive.h"
#include "engine/io/af_layers/geom/af_geom.h"
#include "engine/io/af_layers/geom/af_homography.h"
#include "engine/io/af_layers/graph/af_graph.h"
#include "engine/io/af_layers/graph/af_graph_parser.h"
#include "engine/io/af_layers/image/af_image.h"
#include "engine/io/af_layers/filter/af_blur.h"
#include "engine/io/af_layers/filter/af_distort.h"
#include "engine/io/af_layers/filter/af_fx.h"
#include "engine/io/af_layers/filter/af_live.h"
#include "engine/io/af_layers/vector/af_shapes.h"
#include "engine/io/af_layers/walker/af_walker.h"

namespace pittore::io {
namespace af_detail {


#ifdef PITTORE_AF
std::vector<std::uint8_t> zstdDecode(const std::uint8_t* src, std::size_t srcSize,
                                     std::size_t limit) {
    std::vector<std::uint8_t> out(limit);
    ZSTD_DCtx* ctx = ZSTD_createDCtx();
    if (!ctx) fail("zstd: cannot create decode context");
    ZSTD_inBuffer in{src, srcSize, 0};
    ZSTD_outBuffer ob{out.data(), limit, 0};
    std::size_t rem = 1;
    while (rem != 0) {
        const std::size_t beforeIn = in.pos, beforeOut = ob.pos;
        rem = ZSTD_decompressStream(ctx, &ob, &in);
        if (ZSTD_isError(rem)) {
            const std::string err = ZSTD_getErrorName(rem);
            ZSTD_freeDCtx(ctx);
            fail("zstd: " + err);
        }
        if (rem != 0 && in.pos == beforeIn && ob.pos == beforeOut) break;  // stalled
        if (ob.pos == ob.size) break;                                      // cap reached
    }
    ZSTD_freeDCtx(ctx);
    out.resize(ob.pos);
    return out;
}
#endif


void undoDeltaU8(std::uint8_t* p, std::size_t n) {
    std::uint8_t acc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        acc = static_cast<std::uint8_t>(acc + p[i]);
        p[i] = acc;
    }
}

void undoDeltaU16(std::uint8_t* p, std::size_t n) {
    if (n % 2 != 0) fail("predicted u16 stream has an odd length");
    std::uint16_t acc = 0;
    for (std::size_t i = 0; i < n; i += 2) {
        const std::uint16_t v = std::uint16_t(p[i]) | (std::uint16_t(p[i + 1]) << 8);
        acc = static_cast<std::uint16_t>(acc + v);
        p[i] = static_cast<std::uint8_t>(acc & 0xff);
        p[i + 1] = static_cast<std::uint8_t>(acc >> 8);
    }
}

void undoDeinterleave16(std::uint8_t* p) {
    std::vector<std::uint8_t> out(0x10000);
    for (std::size_t k = 0; k < 0x4000; ++k) {
        out[4 * k + 1] = p[2 * k];
        out[4 * k + 3] = p[2 * k + 1];
        out[4 * k] = p[0x8000 + 2 * k];
        out[4 * k + 2] = p[0x8000 + 2 * k + 1];
    }
    std::memcpy(p, out.data(), out.size());
}


Archive Archive::parse(const std::vector<std::uint8_t>& bytes) {
    Archive a;
    a.data = bytes.data();
    a.size = bytes.size();
    Cursor c(bytes.data(), bytes.size());
    const std::uint8_t* magic = c.take(4);
    if (!(magic[0] == 0x00 && magic[1] == 0xff && magic[2] == 0x4b && magic[3] == 0x41))
        fail("not an Affinity container (bad magic)");
    a.version = c.u16();
    const std::uint16_t flags = c.u16();
    c.u32();  // class tag ("Prsn")
    if (a.version < 7 || a.version > 12)
        fail("unsupported Affinity container version " + std::to_string(a.version));
    if (flags & 3) fail("unsupported container flags");
    if (c.u32() != letag4("#Inf")) fail("missing #Inf block");
    const std::uint64_t fatOffset = c.u64();
    const std::uint64_t thumbOffset = c.u64();
    const std::uint64_t length = c.u64();
    c.u64();  // unknown
    c.u64();  // creation date
    c.u32();  // revision
    c.u32();  // num
    if (a.version > 7) {
        if (c.u32() != letag4("Prot")) fail("missing Prot block");
        c.u32();  // protocol revision
    }
    if (fatOffset > bytes.size() || thumbOffset > bytes.size())
        fail("container pointers are out of range");
    (void)length;  // informational only (not the file length; ignored on read)

    std::size_t offset = static_cast<std::size_t>(fatOffset);
    std::set<std::size_t> visited;
    std::uint32_t blocks = 0;
    while (offset != 0) {
        if (++blocks > 10000) fail("FAT chain does not terminate");
        if (offset > bytes.size() || bytes.size() - offset < 8) fail("FAT block is truncated");
        if (visited.count(offset)) break;
        visited.insert(offset);
        Cursor fc(bytes.data() + offset, bytes.size() - offset);
        const std::uint32_t fatTag = fc.u32();
        if (!(fatTag == letag4("#FAT") || fatTag == letag4("#FT2") || fatTag == letag4("#FT3") ||
              fatTag == letag4("#FT4")))
            fail("bad FAT block tag " + tagName(fatTag));
        const std::uint64_t nextOffset = fc.u64();
        const std::uint64_t savepoint = fc.u64();
        fc.u64();  // some offset
        fc.u64();  // some length
        fc.u64();  // unknown5
        const std::uint32_t filesCount = fc.u32();
        fc.u32();  // unknown7
        fc.u32();  // unknown8
        const std::uint16_t dirsCount = fc.u16();
        fc.u8();   // unknown10
        if (filesCount > fc.remaining()) fail("FAT file count exceeds the block");
        for (std::uint32_t i = 0; i < filesCount; ++i) {
            ArchiveEntry e;
            e.id = fc.u32();
            e.flag = fc.u8();
            e.savepoint = savepoint;
            if (e.flag > 2) fail("bad entry flag " + std::to_string(e.flag));
            if (e.flag != 2) {
                e.offset = fc.u64();
                e.size = fc.u64();
                e.compressedSize = fc.u64();
                e.crc32 = fc.u32();
                e.compression = fc.u8();
                if (fatTag != letag4("#FAT")) fc.u32();  // unknown6
                if (fatTag == letag4("#FT4")) fc.u32();  // unknown
                // Early FATs store an algorithm index; normalize to full form.
                if (fatTag == letag4("#FAT") || fatTag == letag4("#FT2")) {
                    switch (e.compression) {
                        case 0x01: e.compression = 0x01; break;
                        case 0x02: e.compression = 0x41; break;
                        case 0x03: e.compression = 0x81; break;
                        case 0x04: e.compression = 0xc1; break;
                        default: e.compression = 0;
                    }
                }
            }
            if (e.flag == 0) {
                const std::uint16_t len = fc.u16();
                const std::uint8_t* nb = fc.take(len);
                e.name = std::string(reinterpret_cast<const char*>(nb), len);
                auto it = a.names.find(*e.name);
                if (it != a.names.end() && it->second != e.id)
                    fail("entry name maps to two ids: " + *e.name);
                if (it == a.names.end()) a.names.emplace(*e.name, e.id);
            }
            a.entries.push_back(std::move(e));
        }
        for (std::uint16_t i = 0; i < dirsCount; ++i) {
            const std::uint16_t ln = fc.u16();
            const std::uint16_t some = fc.u16();
            fc.u64();
            fc.take(ln);
            if (some != 0) fail("unexpected directory payload");
        }
        offset = static_cast<std::size_t>(nextOffset);
    }
    // Head revision per id, preferring the later-listed savepoint on a tie.
    for (std::size_t i = 0; i < a.entries.size(); ++i) {
        const ArchiveEntry& e = a.entries[i];
        auto h = a.heads.find(e.id);
        if (h == a.heads.end()) {
            a.heads.emplace(e.id, i);
        } else if (e.savepoint >= a.entries[h->second].savepoint) {
            h->second = i;
        }
    }
    return a;
}


std::vector<std::uint8_t> Archive::extract(const ArchiveEntry& e) const {
    if (e.size == 0) fail("empty entry");
    if (e.size > kMaxEntrySize) fail("entry declares more than the size limit");
    Cursor c(data, size);
    c.seek(static_cast<std::size_t>(e.offset));
    if (c.u32() != letag4("#Fil")) fail("entry does not point at a #Fil block");

    const std::uint8_t algorithm = e.compression & 3;
    std::uint8_t predictFlag = (e.compression >> 5) & 1;
    std::uint8_t predictType = 0;
    switch (e.compression & 0xc0) {
        case 0x40: predictFlag = 0; predictType = 1; break;
        case 0x80: predictType = 2; break;
        case 0xc0: predictType = 3; break;
        default: predictFlag = 0; predictType = 0;
    }

    std::vector<std::uint8_t> plain;
    if (algorithm == 1) {
        const std::uint8_t* raw = c.take(static_cast<std::size_t>(e.compressedSize));
        plain.resize(static_cast<std::size_t>(e.size));
        uLongf destLen = static_cast<uLongf>(e.size);
        const int rc =
            uncompress(plain.data(), &destLen, raw, static_cast<uLong>(e.compressedSize));
        if (rc != Z_OK) fail("zlib: entry did not inflate");
        plain.resize(destLen);
    } else if (algorithm == 2) {
#ifdef PITTORE_AF
        const std::uint8_t* raw = c.take(static_cast<std::size_t>(e.compressedSize));
        plain = zstdDecode(raw, static_cast<std::size_t>(e.compressedSize),
                           static_cast<std::size_t>(e.size) + 1);
#else
        fail("entry needs zstd, which this build lacks (PITTORE_AF)");
#endif
    } else {
        const std::uint8_t* raw = c.take(static_cast<std::size_t>(e.size));
        plain.assign(raw, raw + e.size);
    }
    if (plain.size() != e.size)
        fail("entry decompressed to " + std::to_string(plain.size()) + " bytes, expected " +
             std::to_string(e.size));

    if (predictFlag == 0) {
        if (predictType == 1) undoDeltaU8(plain.data(), plain.size());
        if (predictType == 2) undoDeltaU16(plain.data(), plain.size());
    } else if (predictType == 2) {
        undoDeltaU8(plain.data(), plain.size());
        if (plain.size() == 0x10000) undoDeinterleave16(plain.data());
    }

    const uLong crc =
        crc32(0, reinterpret_cast<const Bytef*>(plain.data()), static_cast<uInt>(plain.size()));
    if (static_cast<std::uint32_t>(crc) != e.crc32) fail("CRC mismatch on entry");
    return plain;
}

}  // namespace af_detail
}  // namespace pittore::io
