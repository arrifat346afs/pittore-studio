#pragma once
// Image href kinds. Detects embedded data URIs.
#include <string>

namespace pittore::svg {

bool isDataUri(const std::string& href);
bool isEmbeddedImage(const std::string& href);

}  // namespace pittore::svg
