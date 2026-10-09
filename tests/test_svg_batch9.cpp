// SVG batch9 units: css, save, guards, splits.
#include "engine/vector/svg/attr_escape.h"
#include "engine/vector/svg/attr_trim.h"
#include "engine/vector/svg/bom_strip.h"
#include "engine/vector/svg/cdata_wrap.h"
#include "engine/vector/svg/chunk_split.h"
#include "engine/vector/svg/class_split.h"
#include "engine/vector/svg/comment_skip.h"
#include "engine/vector/svg/decl_merge.h"
#include "engine/vector/svg/depth_guard.h"
#include "engine/vector/svg/grain_split.h"
#include "engine/vector/svg/indent_write.h"
#include "engine/vector/svg/list_split.h"
#include "engine/vector/svg/num_unit.h"
#include "engine/vector/svg/pct_of.h"
#include "engine/vector/svg/ref_guard.h"
#include "engine/vector/svg/row_split.h"
#include "engine/vector/svg/sheet_lookup.h"
#include "engine/vector/svg/size_guard.h"
#include "engine/vector/svg/stripe_join.h"
#include "engine/vector/svg/style_join.h"
#include "engine/vector/svg/tag_count.h"
#include "engine/vector/svg/tag_match.h"
#include "engine/vector/svg/text_escape.h"
#include "engine/vector/svg/use_count.h"
#include "engine/vector/svg/utf16_check.h"
#include "engine/vector/svg/zip_magic.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_css() {
    CHECK_EQ(mergeDecls({{"a", "1"}}, {{"b", "2"}}).size(), 2u);
    CHECK_EQ(splitClasses("a b").size(), 2u);
    CHECK(selectorMatches("#a", "rect", "a", ""));
    CHECK_EQ(splitComma("a,b").size(), 2u);
    CHECK(joinStyle({{"a", "1"}}) == "a:1");
    CHECK(lookupSheet({}, "rect", "", "", "fill").empty());
}

void test_save() {
    CHECK_EQ(trimAttr(" a ").size(), 1u);
    CHECK(splitNumUnit("10px").valid);
    CHECK_NEAR(pctFraction("50%"), 0.5, 1e-9);
    CHECK_EQ(indentPad(1).size(), 2u);
    CHECK(escapeAttr("<") == "&lt;");
    CHECK(escapeText("&") == "&amp;");
    CHECK(!cdataWrap("a<b").empty());
    CHECK(stripCssComments("a/*x*/b") == "ab");
}

void test_guards() {
    const std::uint8_t gz[2] = {0x1f, 0x8b};
    CHECK(hasGzipMagic(gz, 2));
    CHECK(!stripBom("hi").empty());
    CHECK(utf16Kind(gz, 2) == 0);
    CHECK(overSizeCap(100u * 1024u * 1024u));
    CHECK(!overTagCap(10));
    CHECK_EQ(countTags("<a><b>", 100), 2);
    CHECK_EQ(countUses("<use"), 1);
    CHECK(!overDepth(10));
    CHECK(!overRefCap(10));
    CHECK_EQ(splitRanges(10, 3).size(), 3u);
    CHECK_EQ(splitRows(256, 4).size(), 4u);
    CHECK_EQ(pickThreads(1000, 10, 4), 4u);
    CHECK(stripesCover({{0, 64}, {64, 64}}, 128));
}

}  // namespace

int main() {
    test_css();
    test_save();
    test_guards();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
