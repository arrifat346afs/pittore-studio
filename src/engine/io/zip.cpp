#include "engine/io/zip.h"

#include <zlib.h>

namespace pittore::io {
namespace {

constexpr std::uint32_t kLocalSig = 0x04034b50u;
constexpr std::uint32_t kCentralSig = 0x02014b50u;
constexpr std::uint32_t kEocdSig = 0x06054b50u;
constexpr std::size_t kLocalFixed = 30u;

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put16(out, static_cast<std::uint16_t>(v & 0xffff));
    put16(out, static_cast<std::uint16_t>((v >> 16) & 0xffff));
}

std::uint16_t get16(const std::vector<std::uint8_t>& b, std::size_t o) {
    if (o + 2 > b.size()) return 0;
    return static_cast<std::uint16_t>(b[o] | (static_cast<std::uint16_t>(b[o + 1]) << 8));
}

std::uint32_t get32(const std::vector<std::uint8_t>& b, std::size_t o) {
    return static_cast<std::uint32_t>(get16(b, o)) |
           (static_cast<std::uint32_t>(get16(b, o + 2)) << 16);
}

// Raw deflate (no zlib wrapper) — the method ZIP uses.
std::vector<std::uint8_t> deflateRaw(const std::vector<std::uint8_t>& in) {
    z_stream strm{};
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    std::vector<std::uint8_t> out(compressBound(in.empty() ? 1u : in.size()));
    strm.next_in = const_cast<Bytef*>(in.data());
    strm.avail_in = static_cast<uInt>(in.size());
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out.size());
    const int rc = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
    if (rc != Z_STREAM_END) return {};
    out.resize(strm.total_out);
    return out;
}

std::optional<std::vector<std::uint8_t>> inflateRaw(const std::vector<std::uint8_t>& in,
                                                    std::size_t expected) {
    z_stream strm{};
    if (inflateInit2(&strm, -15) != Z_OK) return std::nullopt;
    std::size_t cap = expected > 0 ? expected : in.size() * 8 + 4096;
    std::vector<std::uint8_t> out(cap);
    strm.next_in = const_cast<Bytef*>(in.data());
    strm.avail_in = static_cast<uInt>(in.size());
    int rc;
    do {
        if (strm.total_out == out.size()) out.resize(out.size() * 2);
        strm.next_out = out.data() + strm.total_out;
        strm.avail_out = static_cast<uInt>(out.size() - strm.total_out);
        rc = inflate(&strm, Z_NO_FLUSH);
        if (rc == Z_BUF_ERROR) {
            inflateEnd(&strm);
            return std::nullopt;  // no progress: corrupt stream
        }
    } while (rc == Z_OK);
    inflateEnd(&strm);
    if (rc != Z_STREAM_END) return std::nullopt;
    if (expected > 0 && strm.total_out != expected) return std::nullopt;
    out.resize(strm.total_out);
    return out;
}

}  // namespace

std::optional<std::vector<ZipEntry>> zipRead(const std::vector<std::uint8_t>& archive) {
    if (archive.size() < 22) return std::nullopt;

    // End-of-central-directory is the smallest structure with the right total
    // size (22 bytes); its comment may extend up to 64 KiB, so scan backwards
    // from the tail.
    std::size_t eocd = std::string::npos;
    const std::size_t scanStart =
        archive.size() > 22 + 65535 ? archive.size() - (22 + 65535) : 0;
    for (std::size_t i = archive.size() - 22 + 1; i-- > scanStart;) {
        if (get32(archive, i) == kEocdSig) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) return std::nullopt;

    const std::uint32_t entryCount = get16(archive, eocd + 10);
    const std::uint32_t cdSize = get32(archive, eocd + 12);
    const std::uint32_t cdOffset = get32(archive, eocd + 16);
    if (static_cast<std::uint64_t>(cdOffset) + cdSize > archive.size())
        return std::nullopt;

    std::vector<ZipEntry> out;
    std::size_t pos = cdOffset;
    for (std::uint32_t n = 0; n < entryCount; ++n) {
        if (pos + 46 > archive.size() || get32(archive, pos) != kCentralSig)
            return std::nullopt;
        const std::uint16_t method = get16(archive, pos + 10);
        const std::uint32_t crc = get32(archive, pos + 16);
        const std::uint32_t compSize = get32(archive, pos + 20);
        const std::uint32_t uncompSize = get32(archive, pos + 24);
        const std::uint16_t nameLen = get16(archive, pos + 28);
        const std::uint16_t extraLen = get16(archive, pos + 30);
        const std::uint16_t commentLen = get16(archive, pos + 32);
        const std::uint32_t localOffset = get32(archive, pos + 42);
        if (method != 0 && method != 8) return std::nullopt;  // unsupported compression
        if (pos + 46 + nameLen + extraLen + commentLen > archive.size())
            return std::nullopt;

        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(archive.data() + pos + 46), nameLen);
        entry.method = method;
        pos += 46 + nameLen + extraLen + commentLen;

        // Pure directory entry (name ends with '/', no data).
        if (!entry.name.empty() && entry.name.back() == '/' && uncompSize == 0) {
            out.push_back(std::move(entry));
            continue;
        }

        if (localOffset + kLocalFixed > archive.size() ||
            get32(archive, localOffset) != kLocalSig)
            return std::nullopt;
        const std::uint16_t lNameLen = get16(archive, localOffset + 26);
        const std::uint16_t lExtraLen = get16(archive, localOffset + 28);
        const std::size_t dataPos = localOffset + kLocalFixed + lNameLen + lExtraLen;
        if (dataPos + compSize > archive.size()) return std::nullopt;

        const std::vector<std::uint8_t> comp(archive.begin() +
                                                 static_cast<std::ptrdiff_t>(dataPos),
                                             archive.begin() +
                                                 static_cast<std::ptrdiff_t>(dataPos + compSize));
        if (method == 0) {
            if (comp.size() != uncompSize) return std::nullopt;
            entry.data = comp;
        } else {
            std::optional<std::vector<std::uint8_t>> dec = inflateRaw(comp, uncompSize);
            if (!dec) return std::nullopt;
            entry.data = std::move(*dec);
        }
        const std::uint32_t got =
            crc32(0, entry.data.data(), static_cast<uInt>(entry.data.size()));
        if (got != crc) return std::nullopt;
        out.push_back(std::move(entry));
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> zipWrite(const std::vector<ZipEntry>& entries) {
    struct CdRec {
        std::uint32_t offset, crc, compSize, uncompSize;
        std::uint16_t method;
        std::string name;
    };
    std::vector<std::uint8_t> out;
    out.reserve(1024);
    std::vector<CdRec> cd;
    cd.reserve(entries.size());

    for (const ZipEntry& e : entries) {
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        std::vector<std::uint8_t> comp;
        if (e.method == 0) {
            comp = e.data;
        } else {
            comp = deflateRaw(e.data);
            if (comp.empty() && !e.data.empty()) return std::nullopt;
        }
        const std::uint32_t crc = crc32(0, e.data.data(), static_cast<uInt>(e.data.size()));
        const std::uint32_t compSize = static_cast<std::uint32_t>(comp.size());
        const std::uint32_t uncompSize = static_cast<std::uint32_t>(e.data.size());

        put32(out, kLocalSig);
        put16(out, 20);  // version needed
        put16(out, 0);   // flags
        put16(out, e.method);
        put16(out, 0);  // mod time
        put16(out, 0);  // mod date
        put32(out, crc);
        put32(out, compSize);
        put32(out, uncompSize);
        put16(out, static_cast<std::uint16_t>(e.name.size()));
        put16(out, 0);  // extra len
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), comp.begin(), comp.end());

        cd.push_back({offset, crc, compSize, uncompSize, e.method, e.name});
    }

    const std::uint32_t cdOffset = static_cast<std::uint32_t>(out.size());
    for (const CdRec& r : cd) {
        put32(out, kCentralSig);
        put16(out, 20);  // version made by
        put16(out, 20);  // version needed
        put16(out, 0);   // flags
        put16(out, r.method);
        put16(out, 0);  // mod time
        put16(out, 0);  // mod date
        put32(out, r.crc);
        put32(out, r.compSize);
        put32(out, r.uncompSize);
        put16(out, static_cast<std::uint16_t>(r.name.size()));
        put16(out, 0);  // extra len
        put16(out, 0);  // comment len
        put16(out, 0);  // disk start
        put16(out, 0);  // internal attrs
        put32(out, 0);  // external attrs
        put32(out, r.offset);
        out.insert(out.end(), r.name.begin(), r.name.end());
    }

    const std::uint32_t cdSize = static_cast<std::uint32_t>(out.size() - cdOffset);
    put32(out, kEocdSig);
    put16(out, 0);  // disk number
    put16(out, 0);  // disk with central dir
    put16(out, static_cast<std::uint16_t>(cd.size()));
    put16(out, static_cast<std::uint16_t>(cd.size()));
    put32(out, cdSize);
    put32(out, cdOffset);
    put16(out, 0);  // comment len
    return out;
}

const ZipEntry* zipFind(const std::vector<ZipEntry>& entries, const std::string& name) {
    for (const ZipEntry& e : entries) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

}  // namespace pittore::io