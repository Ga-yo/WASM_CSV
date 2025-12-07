#ifndef CSV_UTILS_H
#define CSV_UTILS_H

#include <string>

std::string trim(const std::string& str);
std::string removeBOM(const std::string& str);
std::string normalizeLineEndings(const std::string& str);
std::string escapeJson(const std::string& str);
std::string cleanNumericString(const std::string& input);

#endif // CSV_UTILS_H