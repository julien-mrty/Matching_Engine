#include "utils/strings.hpp"
#include <algorithm>
#include <cctype>

std::string to_upper_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return s;
}
