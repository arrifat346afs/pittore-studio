// CDATA only when markup present.
#include "engine/vector/svg/cdata_wrap.h"

namespace pittore::svg {

std::string cdataWrap(const std::string& s) {
    if (s.find('<') == std::string::npos && s.find('&') == std::string::npos) {
        return s;
    }
    return "<![CDATA[" + s + "]]>";
}

}  // namespace pittore::svg
