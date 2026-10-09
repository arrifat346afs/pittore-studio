// Split on last colon.
#include "engine/vector/svg/ns_name.h"

namespace pittore::svg {

std::string localName(const std::string& qname) {
    const size_t p = qname.rfind(':');
    return p == std::string::npos ? qname : qname.substr(p + 1);
}

std::string prefixOf(const std::string& qname) {
    const size_t p = qname.rfind(':');
    return p == std::string::npos ? std::string() : qname.substr(0, p);
}

}  // namespace pittore::svg
