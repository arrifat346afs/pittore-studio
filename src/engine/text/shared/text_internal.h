#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <hb-ft.h>
#include <hb.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "engine/text/text_engine.h"

namespace pittore::text {
namespace detail {

inline std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Face {
    FT_Face ft = nullptr;
    hb_font_t* hb = nullptr;
    std::optional<float> capRatio;
    ~Face() {
        if (hb) hb_font_destroy(hb);
        if (ft) FT_Done_Face(ft);
    }
};

struct ShapedGlyph {
    unsigned id = 0;
    unsigned cluster = 0;
    float xAdvance = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
};

struct GlyphStyle {
    float size = 48.0f;
    float hScale = 1.0f;
    float vScale = 1.0f;
    float shift = 0.0f;  // baseline shift, positive = up
};

struct GlyphBitmap {
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
    std::vector<std::uint8_t> data;
};

struct PlacedGlyph {
    unsigned glyph = 0;
    float x = 0.0f;  // pen origin, layout space
    float baseline = 0.0f;
};

struct Layout {
    std::vector<PlacedGlyph> glyphs;
    std::vector<LineSpan> lines;
    std::vector<TextLayout::CharPos> chars;
    float firstBaseline = 0.0f;
    float lineAdvance = 0.0f;
    float layoutWidth = 0.0f;
};

FT_Library library();
std::shared_ptr<Face> loadFace(const std::string& family, bool bold, bool italic);
std::shared_ptr<Face> cachedFace(const std::string& family, bool bold, bool italic);
void setSize(Face& f, float size);
std::vector<ShapedGlyph> shape(Face& f, const std::string& text, float size,
                               const TextSpec& spec);
std::string displayText(const TextSpec& spec);
GlyphStyle glyphStyle(const TextSpec& spec);
bool renderGlyph(Face& f, unsigned gid, GlyphBitmap& out);
std::size_t charCount(const std::string& s);
float advanceForRange(const std::vector<ShapedGlyph>& shaped, std::size_t from,
                      std::size_t len, float tracking, std::size_t chars,
                      float hScale);
Layout layout(const TextSpec& spec, Face& face);
const std::unordered_set<std::string>& familySet();
const std::vector<std::string>& familyList();
std::size_t clampBoundary(const std::string& s, std::size_t byte);

}  // namespace detail
}  // namespace pittore::text
