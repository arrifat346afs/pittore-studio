#pragma once
// Embedded ICC profile description reader (clean-room implementation).
// Shared by the format codecs that surface embedded profiles for the import
// mismatch policy (PSD 0x0422 resources, TIFF ICCPROFILE tags): reads the
// profile's 'desc' tag — textDescriptionType, multiLocalizedUnicodeType
// ('mluc', enUS preferred, first decodable otherwise), or plain 'text'.
// Fully bounds-checked, never throws; hostile/truncated blobs yield "".
// Header-only so engine tests can exercise it without linking a codec.

#include <cstdint>
#include <cstddef>
#include <string>

namespace pittore::io {
namespace icc_detail {

inline bool peekU16(const std::uint8_t* p, std::size_t n, std::size_t off,
                    std::uint16_t* out) {
    if (off + 2 > n) return false;
    *out = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[off]) << 8) |
                                      p[off + 1]);
    return true;
}

inline bool peekU32(const std::uint8_t* p, std::size_t n, std::size_t off,
                    std::uint32_t* out) {
    if (off + 4 > n) return false;
    *out = (static_cast<std::uint32_t>(p[off]) << 24) |
           (static_cast<std::uint32_t>(p[off + 1]) << 16) |
           (static_cast<std::uint32_t>(p[off + 2]) << 8) |
           static_cast<std::uint32_t>(p[off + 3]);
    return true;
}

// UTF-16BE slice -> ASCII, or "" when any unit is non-ASCII-printable (keeps
// hostile profiles from producing control characters in dialogs).
inline std::string utf16beToAscii(const std::uint8_t* p, std::size_t units) {
    std::string out;
    out.reserve(units);
    for (std::size_t i = 0; i < units; ++i) {
        const unsigned hi = p[i * 2], lo = p[i * 2 + 1];
        if (hi != 0 || lo == 0 || (lo < 32 && lo != 9) || lo > 126) return "";
        out.push_back(static_cast<char>(lo));
    }
    return out;
}

// Description from one ICC 'desc' tag at [tag, tag+tagSize).
inline std::string descriptionFromTag(const std::uint8_t* icc, std::size_t len,
                                      std::uint32_t tag, std::uint32_t tagSize) {
    std::uint32_t type = 0;
    if (!peekU32(icc, len, tag, &type)) return "";
    if (type == 0x64657363) {  // 'desc' textDescriptionType
        std::uint32_t count = 0;
        if (!peekU32(icc, len, tag + 8, &count)) return "";
        // ASCII bytes live at tag+12; count includes the NUL.
        if (count == 0 || count > tagSize || 12 + count > tagSize) return "";
        std::string out;
        for (std::uint32_t i = 0; i < count - 1; ++i) {
            const unsigned ch = icc[tag + 12 + i];
            if (ch == 0) break;
            if (ch < 32 || ch > 126) return "";
            out.push_back(static_cast<char>(ch));
        }
        return out;
    }
    if (type == 0x6d6c7563) {  // 'mluc'
        std::uint32_t records = 0, recSize = 0;
        if (!peekU32(icc, len, tag + 8, &records)) return "";
        if (!peekU32(icc, len, tag + 12, &recSize)) return "";
        if (records == 0 || records > 64 || recSize < 12) return "";
        if (16 + records * recSize > tagSize) return "";
        std::string fallback;
        for (std::uint32_t r = 0; r < records; ++r) {
            const std::size_t rec = tag + 16 + r * recSize;
            std::uint32_t lang = 0, strLen = 0, strOff = 0;
            if (!peekU32(icc, len, rec, &lang)) return "";
            if (!peekU32(icc, len, rec + 4, &strLen)) return "";
            if (!peekU32(icc, len, rec + 8, &strOff)) return "";
            // String offsets are relative to the tag start; lengths are
            // bytes (UTF-16BE => even) and must stay inside the tag.
            if (strLen == 0 || strLen > 512 || (strLen & 1)) continue;
            if (strOff > tagSize || strLen > tagSize - strOff) continue;
            const std::string text =
                utf16beToAscii(icc + tag + strOff, strLen / 2);
            if (text.empty()) continue;
            if (fallback.empty()) fallback = text;
            if (lang == 0x656E5553) return text;  // 'enUS'
        }
        return fallback;
    }
    if (type == 0x74657874) {  // 'text'
        if (tagSize < 8) return "";
        std::string out;
        for (std::uint32_t i = 0; i + 8 < tagSize; ++i) {
            const unsigned ch = icc[tag + 8 + i];
            if (ch == 0) break;
            if (ch < 32 || ch > 126) return "";
            out.push_back(static_cast<char>(ch));
        }
        return out;
    }
    return "";
}

}  // namespace icc_detail

// Profile description from a raw ICC blob, or "" when absent/unreadable.
inline std::string iccProfileDescription(const std::uint8_t* p,
                                         std::size_t n) {
    if (n < 132) return "";
    std::uint32_t count = 0;
    if (!icc_detail::peekU32(p, n, 128, &count)) return "";
    if (count == 0 || count > 64) return "";
    if (132 + static_cast<std::size_t>(count) * 12 > n) return "";
    for (std::uint32_t t = 0; t < count; ++t) {
        std::uint32_t sig = 0, off = 0, size = 0;
        const std::size_t ent = 132 + t * 12;
        if (!icc_detail::peekU32(p, n, ent, &sig)) return "";
        if (!icc_detail::peekU32(p, n, ent + 4, &off)) return "";
        if (!icc_detail::peekU32(p, n, ent + 8, &size)) return "";
        if (sig != 0x64657363) continue;  // 'desc'
        if (off > n || size > n - off) return "";
        return icc_detail::descriptionFromTag(p, n, off, size);
    }
    return "";
}

}  // namespace pittore::io
