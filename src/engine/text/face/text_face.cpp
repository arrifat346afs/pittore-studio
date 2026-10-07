#include "engine/text/text_engine.h"

#include <hb-ft.h>
#include <hb-ot.h>
#include <hb.h>

#include <fontconfig/fontconfig.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/text/shared/text_internal.h"

namespace pittore::text {
namespace detail {

FT_Library library() {
    static FT_Library lib = [] {
        FT_Library l = nullptr;
        FT_Init_FreeType(&l);
        return l;
    }();
    return lib;
}

std::shared_ptr<Face> loadFace(const std::string& family, bool bold, bool italic) {
    FT_Library lib = library();
    if (!lib) return nullptr;

    FcPattern* pat = FcPatternCreate();
    if (!pat) return nullptr;
    FcPatternAddString(pat, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcConfigSubstitute(nullptr, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pat, &res);
    std::string file;
    int index = 0;
    if (match) {
        FcChar8* f = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &f) == FcResultMatch && f)
            file = reinterpret_cast<const char*>(f);
        int idx = 0;
        if (FcPatternGetInteger(match, FC_INDEX, 0, &idx) == FcResultMatch) index = idx;
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    if (file.empty()) return nullptr;

    auto face = std::make_shared<Face>();
    if (FT_New_Face(lib, file.c_str(), index, &face->ft) != 0) {
        if (FT_New_Face(lib, file.c_str(), 0, &face->ft) != 0) return nullptr;
    }
    hb_face_t* hbFace = hb_ft_face_create_referenced(face->ft);
    if (!hbFace) return nullptr;
    face->hb = hb_font_create(hbFace);
    hb_face_destroy(hbFace);
    if (!face->hb) return nullptr;
    hb_ot_font_set_funcs(face->hb);

    if (auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face->ft, ft_sfnt_os2))) {
        if (os2->version != 0xFFFF && os2->sCapHeight > 0 && face->ft->units_per_EM > 0)
            face->capRatio = static_cast<float>(os2->sCapHeight) /
                             static_cast<float>(face->ft->units_per_EM);
    }
    return face;
}

std::shared_ptr<Face> cachedFace(const std::string& family, bool bold, bool italic) {
    static std::mutex mu;
    static std::unordered_map<std::string, std::shared_ptr<Face>> cache;
    const std::string key =
        toLower(family) + "|" + (bold ? "b" : "n") + (italic ? "i" : "u");
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    auto face = loadFace(family, bold, italic);
    cache.emplace(key, face);
    return face;
}

void setSize(Face& f, float size) {
    const float px = std::max(0.01f, size);
    FT_Set_Char_Size(f.ft, 0, static_cast<FT_F26Dot6>(std::lround(px * 64.0)),
                     0, 0);
    const int s = static_cast<int>(std::lround(px * 64.0));
    hb_font_set_scale(f.hb, s, s);
    const unsigned ppem = static_cast<unsigned>(std::max(1, static_cast<int>(std::lround(px))));
    hb_font_set_ppem(f.hb, ppem, ppem);
}

}  // namespace detail
}  // namespace pittore::text
