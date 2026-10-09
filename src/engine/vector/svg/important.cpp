// Trailing bang only.
#include "engine/vector/svg/important.h"

namespace pittore::svg {

DeclVal splitImportant(std::string s) {
    DeclVal o;
    while (!s.empty() && s.back() == ' ') {
        s.pop_back();
    }
    const size_t p = s.rfind("!important");
    if (p == std::string::npos) {
        o.value = s;
        return o;
    }
    o.important = true;
    o.value = s.substr(0, p);
    while (!o.value.empty() && o.value.back() == ' ') {
        o.value.pop_back();
    }
    return o;
}

}  // namespace pittore::svg
