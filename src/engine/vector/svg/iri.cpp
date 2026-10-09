// IRI parse. Trims quotes, blanks, close paren.
#include "engine/vector/svg/iri.h"

#include <cctype>

namespace pittore::svg {

std::string normalizeIri(std::string_view v) {
    size_t a = 0;
    while (a < v.size() && (std::isspace((unsigned char)v[a]) || v[a] == '#' ||
                            v[a] == '"' || v[a] == '\'')) {
        if (v[a] == '#') {
            ++a;
            break;
        }
        ++a;
    }
    if (!v.empty() && v[0] == '#') {
        a = 1;
    }
    size_t b = v.size();
    while (b > a && (std::isspace((unsigned char)v[b - 1]) || v[b - 1] == '"' ||
                     v[b - 1] == '\'' || v[b - 1] == ')')) {
        --b;
    }
    return std::string(v.substr(a, b > a ? b - a : 0));
}

std::string refTarget(std::string_view v) {
    std::string s;
    s.reserve(v.size());
    size_t a = 0;
    while (a < v.size() && std::isspace((unsigned char)v[a])) {
        ++a;
    }
    s = std::string(v.substr(a));
    if (s.compare(0, 4, "url(") != 0) {
        return "";
    }
    const size_t h = s.find('#');
    if (h == std::string::npos) {
        return "";
    }
    size_t e = s.find(')', h);
    return normalizeIri(s.substr(h + 1, e == std::string::npos
                                             ? std::string::npos
                                             : e - h - 1));
}

}  // namespace pittore::svg
