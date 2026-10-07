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

const std::unordered_set<std::string>& familySet() {
    static const std::unordered_set<std::string> set = [] {
        std::unordered_set<std::string> out;
        FcPattern* pat = FcPatternCreate();
        FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, static_cast<char*>(nullptr));
        FcFontSet* fs = (pat && os) ? FcFontList(nullptr, pat, os) : nullptr;
        if (fs) {
            for (int i = 0; i < fs->nfont; ++i) {
                for (int k = 0;; ++k) {
                    FcChar8* fam = nullptr;
                    if (FcPatternGetString(fs->fonts[i], FC_FAMILY, k, &fam) != FcResultMatch ||
                        !fam)
                        break;
                    out.insert(toLower(reinterpret_cast<const char*>(fam)));
                }
            }
            FcFontSetDestroy(fs);
        }
        if (os) FcObjectSetDestroy(os);
        if (pat) FcPatternDestroy(pat);
        return out;
    }();
    return set;
}

const std::vector<std::string>& familyList() {
    static const std::vector<std::string> list = [] {
        std::unordered_set<std::string> seen;
        std::vector<std::string> out;
        FcPattern* pat = FcPatternCreate();
        FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, static_cast<char*>(nullptr));
        FcFontSet* fs = (pat && os) ? FcFontList(nullptr, pat, os) : nullptr;
        if (fs) {
            for (int i = 0; i < fs->nfont; ++i) {
                for (int k = 0;; ++k) {
                    FcChar8* fam = nullptr;
                    if (FcPatternGetString(fs->fonts[i], FC_FAMILY, k, &fam) != FcResultMatch ||
                        !fam)
                        break;
                    const std::string name(reinterpret_cast<const char*>(fam));
                    if (name.empty()) continue;
                    if (!seen.insert(toLower(name)).second) continue;
                    out.push_back(name);
                }
            }
            FcFontSetDestroy(fs);
        }
        if (os) FcObjectSetDestroy(os);
        if (pat) FcPatternDestroy(pat);
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
            const std::string la = toLower(a);
            const std::string lb = toLower(b);
            return la != lb ? la < lb : a < b;
        });
        return out;
    }();
    return list;
}

}  // namespace detail
}  // namespace pittore::text

namespace pittore::text {

bool hasFamily(const std::string& name) {
    using namespace detail;
    if (name.empty()) return false;
    return familySet().count(toLower(name)) != 0;
}

std::string defaultFamily() {
    using namespace detail;
    static const std::string cached = [] {
        std::string out;
        FcPattern* pat = FcPatternCreate();
        if (pat) {
            FcPatternAddString(pat, FC_FAMILY,
                               reinterpret_cast<const FcChar8*>("sans-serif"));
            FcConfigSubstitute(nullptr, pat, FcMatchPattern);
            FcDefaultSubstitute(pat);
            FcResult res = FcResultNoMatch;
            FcPattern* match = FcFontMatch(nullptr, pat, &res);
            if (match) {
                FcChar8* fam = nullptr;
                if (FcPatternGetString(match, FC_FAMILY, 0, &fam) == FcResultMatch && fam)
                    out = reinterpret_cast<const char*>(fam);
                FcPatternDestroy(match);
            }
            FcPatternDestroy(pat);
        }
        if (out.empty()) out = "sans-serif";
        return out;
    }();
    return cached;
}

std::vector<std::string> familyNames() { return detail::familyList(); }

}  // namespace pittore::text
