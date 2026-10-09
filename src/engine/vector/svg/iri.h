#pragma once
// IRI helpers. url(#id) to id.
#include <string>
#include <string_view>

namespace pittore::svg {

std::string normalizeIri(std::string_view v);
std::string refTarget(std::string_view v);

}  // namespace pittore::svg
