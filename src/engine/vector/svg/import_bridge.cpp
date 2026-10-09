// Intake: strip BOM, read, sheet walk, build, expand, check.
#include "engine/vector/svg/import_bridge.h"

#include <functional>

#include "engine/vector/svg/bom_strip.h"
#include "engine/vector/svg/css_parse.h"
#include "engine/vector/svg/use_expand.h"
#include "engine/vector/svg/xml_reader.h"

namespace pittore::svg {
namespace {

// Collect every style block in the tree.
void sheetWalk(const XmlNode& e, std::vector<CssRule>& sheet) {
    if (e.tag == "style" && !e.text.empty()) {
        for (const auto& r : parseStylesheet(e.text)) {
            sheet.push_back(r);
        }
    }
    for (const auto& c : e.children) {
        sheetWalk(*c, sheet);
    }
}

}  // namespace

ImportedSvg importSvg(const std::string& xml) {
    ImportedSvg out;
    const XmlRead r = readXml(stripBom(xml));
    if (!r.ok || !r.root) {
        out.error = r.error;
        return out;
    }
    std::vector<CssRule> sheet;
    sheetWalk(*r.root, sheet);
    out.scene = buildScene(*r.root, sheet);
    if (!out.scene.ok) {
        out.error = "empty scene";
        return out;
    }
    expandUses(*out.scene.root);
    out.issues = validateScene(out.scene);
    out.items = flattenScene(out.scene);
    out.stats = collectStats(*r.root);
    out.ok = true;
    return out;
}

}  // namespace pittore::svg
