// Lengths by ref.
#include "engine/vector/svg/dxdy_list.h"

#include "engine/vector/svg/length.h"
#include "engine/vector/svg/number_list.h"

namespace pittore::svg {

std::vector<double> parsePosList(const std::string& s, double ref) {
    std::vector<double> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\n')) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != ',' && s[j] != '\t' &&
               s[j] != '\n') {
            ++j;
        }
        out.push_back(readLength(s.substr(i, j - i), ref, 0));
        i = j;
    }
    return out;
}

}  // namespace pittore::svg
