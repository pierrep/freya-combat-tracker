#include "core/text.h"

#include <algorithm>
#include <cctype>

namespace combat {

std::string asciiLower(std::string text)
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool isBlank(const std::string& text)
{
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

std::string trim(std::string text)
{
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

bool containsInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) {
        return true;
    }
    return asciiLower(haystack).find(asciiLower(needle)) != std::string::npos;
}

int compareNames(const std::string& left, const std::string& right)
{
    const std::string a = asciiLower(left);
    const std::string b = asciiLower(right);
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

bool equalsInsensitive(const std::string& left, const std::string& right)
{
    return left.size() == right.size() && compareNames(left, right) == 0;
}

}  // namespace combat
