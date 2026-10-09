// SVG XML and CSS units: tree shape, entities, style lists.
#include <string>

#include "engine/vector/svg/css_parse.h"
#include "engine/vector/svg/xml_reader.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_nested_attrs() {
    const auto r = readXml("<svg width=\"10\"><g id=\"a\"><rect x=\"1\"/></g></svg>");
    CHECK(r.ok);
    CHECK(r.root->tag == "svg");
    CHECK(r.root->attrs["width"] == "10");
    CHECK_EQ(r.root->children.size(), 1u);
    CHECK(r.root->children[0]->tag == "g");
    CHECK(r.root->children[0]->children[0]->tag == "rect");
}

void test_entities() {
    const auto r = readXml("<svg><t>a &amp; b &lt;c&gt;</t></svg>");
    CHECK(r.ok);
    CHECK(r.root->children[0]->text == "a & b <c>");
}

void test_skips() {
    const auto r = readXml("<?xml?><!--c--><!DOCTYPE x><svg><rect/></svg>");
    CHECK(r.ok);
    CHECK(r.root->tag == "svg");
}

void test_cdata() {
    const auto r = readXml("<svg><style><![CDATA[a>b]]></style></svg>");
    CHECK(r.ok);
    CHECK(r.root->children[0]->text == "a>b");
}

void test_unclosed() {
    const auto r = readXml("<svg><g><rect/></g>");
    CHECK(r.ok);
    CHECK(r.root->tag == "svg");
}

void test_bad_tag() {
    const auto r = readXml("<>");
    CHECK(!r.ok);
}

void test_style_decls() {
    const auto m = parseStyleDecls("fill:red; stroke : blue ; ;");
    CHECK(m.at("fill") == "red");
    CHECK(m.at("stroke") == "blue");
}

void test_sheet_split() {
    const auto v = parseStylesheet("/*c*/.a, #b {fill:red;}");
    CHECK_EQ(v.size(), 2u);
    CHECK(v[0].selector == ".a");
    CHECK(v[1].selector == "#b");
    CHECK(v[0].decls.at("fill") == "red");
}

}  // namespace

int main() {
    test_nested_attrs();
    test_entities();
    test_skips();
    test_cdata();
    test_unclosed();
    test_bad_tag();
    test_style_decls();
    test_sheet_split();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
