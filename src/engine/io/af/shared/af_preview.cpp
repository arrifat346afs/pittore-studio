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


std::uint32_t u32be(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}


std::uint32_t u32le(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}


bool hasBytes(const std::vector<std::uint8_t>& b, const std::uint8_t* m,
              std::size_t mlen, std::size_t at) {
    return at + mlen <= b.size() && std::memcmp(b.data() + at, m, mlen) == 0;
}


// ---------------------------------------------------------------------------
// Tail JSON metadata ("Meta" block ends the file with a plaintext JSON object).
// ---------------------------------------------------------------------------

std::string jsonTailString(const std::vector<std::uint8_t>& b, const char* key) {
    // The manifest lives at the very end of the file; never search the whole
    // buffer (compressed frames could contain the key by chance).
    const std::size_t from = b.size() > 65536 ? b.size() - 65536 : 0;
    const std::string pat = std::string("\"") + key + "\":\"";
    const char* tail = reinterpret_cast<const char*>(b.data() + from);
    const std::size_t tailLen = b.size() - from;
    std::string::size_type p = std::string(tail, tailLen).find(pat);
    if (p == std::string::npos) return {};
    p += pat.size();
    std::string v;
    bool esc = false;
    for (; p < tailLen && tail[p] != '"'; ++p) {
        if (esc) {
            v += tail[p] == 'n' ? '\n' : (tail[p] == 't' ? '\t' : tail[p]);
            esc = false;
        } else if (tail[p] == '\\') {
            esc = true;
        } else {
            v += tail[p];
        }
    }
    return v;
}


std::uint32_t jsonTailU32(const std::vector<std::uint8_t>& b, const char* key) {
    const std::size_t from = b.size() > 65536 ? b.size() - 65536 : 0;
    const std::string pat = std::string("\"") + key + "\":";
    const char* tail = reinterpret_cast<const char*>(b.data() + from);
    const std::size_t tailLen = b.size() - from;
    std::string::size_type p = std::string(tail, tailLen).find(pat);
    if (p == std::string::npos) return 0;
    p += pat.size();
    std::uint32_t v = 0;
    bool any = false;
    while (p < tailLen && tail[p] >= '0' && tail[p] <= '9') {
        v = v * 10 + std::uint32_t(tail[p] - '0');
        any = true;
        ++p;
    }
    return any ? v : 0;
}

}  // namespace af_preview
}  // namespace pittore::io
