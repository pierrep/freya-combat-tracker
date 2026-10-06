#pragma once

#include <string>

namespace combat {

// ASCII-only helpers shared by the catalogs, the roster, and the data layer.
std::string asciiLower(std::string text);
bool isBlank(const std::string& text);
std::string trim(std::string text);
// Case-insensitive substring. An empty needle matches everything.
bool containsInsensitive(const std::string& haystack, const std::string& needle);
// Case-insensitive ordering: negative, zero, or positive.
int compareNames(const std::string& left, const std::string& right);
bool equalsInsensitive(const std::string& left, const std::string& right);

}  // namespace combat
