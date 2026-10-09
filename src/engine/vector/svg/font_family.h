#pragma once
// Family list split. First name wins.
#include <string>
#include <vector>

namespace pittore::svg {

std::vector<std::string> splitFamilies(const std::string& s);
std::string firstFamily(const std::string& s);

}  // namespace pittore::svg
