#pragma once
// Style text to rules. Keeps order for cascade.
#include <map>
#include <string>
#include <vector>

namespace pittore::svg {

// One selector plus its declarations.
struct CssRule {
    std::string selector;
    std::map<std::string, std::string> decls;
};

// Parse `prop: value; ...` lists.
std::map<std::string, std::string> parseStyleDecls(const std::string& style);

// Parse `selector { ... }` blocks in order.
std::vector<CssRule> parseStylesheet(const std::string& css);

}  // namespace pittore::svg
