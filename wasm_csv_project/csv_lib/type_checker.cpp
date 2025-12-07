#include "type_checker.h"
#include <algorithm>
#include <cmath>
#include <cerrno>

using namespace std;

static double stringToDouble(const string& str) {
    if (str.empty()) return NAN;
    const char* c_str = str.c_str();
    char* end = nullptr;
    errno = 0; 

    double num = strtod(c_str, &end);

    if (end == c_str || *end != '\0' || errno == ERANGE) {
        return NAN;
    }
    return num;
}

bool TypeChecker::isInteger(const string& str) {
    double num = stringToDouble(str);
    return !isnan(num) && trunc(num) == num;
}

bool TypeChecker::isFloat(const string& str) {
    double num = stringToDouble(str);
    return !isnan(num) && trunc(num) != num;
}

bool TypeChecker::isNumeric(const string& str) {
    return !isnan(stringToDouble(str));
}

// Helper for case-insensitive string comparison without creating a new string
inline bool iequals(const string& a, const string& b) {
    return equal(a.begin(), a.end(),
                 b.begin(), b.end(),
                 [](char a, char b) {
                     return tolower(a) == tolower(b);
                 });
}

bool TypeChecker::isBoolean(const string& str) {
    if (str.length() > 5) return false;
    return iequals(str, "true") || iequals(str, "false") ||
           iequals(str, "yes") || iequals(str, "no") ||
           str == "1" || str == "0";
}

bool TypeChecker::isDate(const string& str) {
    if (str.length() < 8 || str.length() > 10) return false;
    // Simplified check for YYYY-MM-DD or YYYY/MM/DD
    if (str.length() == 10) {
        return (str[4] == '-' || str[4] == '/') && (str[7] == '-' || str[7] == '/');
    }
    // Add more date format checks if needed
    return false;
}

bool TypeChecker::isNull(const string& str) {
    if (str.empty()) return true;
    if (str.length() > 4) return false;
    return iequals(str, "null") ||
           iequals(str, "na") ||
           iequals(str, "n/a") ||
           iequals(str, "nan") ||
           str == "-";
}