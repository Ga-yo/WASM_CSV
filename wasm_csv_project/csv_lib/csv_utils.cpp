#include "csv_utils.h"
#include "type_checker.h"
#include <regex>

using namespace std;

string trim(const string& str) {
    const char* whitespace = " \t\r\n";
    size_t first = str.find_first_not_of(whitespace);
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, last - first + 1);
}

string removeBOM(const string& str) {
    if (str.length() >= 3 &&
        (unsigned char)str[0] == 0xEF &&
        (unsigned char)str[1] == 0xBB &&
        (unsigned char)str[2] == 0xBF) {
        return str.substr(3);
    }
    return str;
}

string normalizeLineEndings(const string& str) {
    string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '\r') {
            if (i + 1 < str.length() && str[i + 1] == '\n') {
                result += '\n';
                i++;
            } else {
                result += '\n';
            }
        } else {
            result += str[i];
        }
    }
    return result;
}

string escapeJson(const string& str) {
    string result;
    // 빠른 경로: 이스케이프가 필요없는 경우 바로 반환
    size_t first_special = str.find_first_of("\"\\\b\f\n\r\t");
    if (first_special == string::npos) {
        return str;
    }

    result.reserve(str.size() * 1.2);
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

// Faster, non-regex implementation of cleanNumericString
string cleanNumericString(const string& input) {
    string result;
    result.reserve(input.length());
    bool foundDigit = false;
    bool foundDecimal = false;
    bool foundSign = false;

    size_t start = input.find_first_not_of(" \t");
    if (start == string::npos) return ""; // Empty or whitespace

    for (size_t i = start; i < input.length(); ++i) {
        char c = input[i];

        if (isdigit(c)) {
            result += c;
            foundDigit = true;
        } else if (c == '.' && !foundDecimal) {
            // Append decimal point only if it seems to be part of a number
            if (result.empty()) result += '0';
            result += c;
            foundDecimal = true;
        } else if ((c == '+' || c == '-') && !foundDigit && !foundSign) {
            result += c;
            foundSign = true;
        } else if (c == ',' || c == ' ') {
            // Ignore commas and spaces, but only if we are already building a number
            if (foundDigit) continue;
        } else {
            // If we have found digits and encounter a non-numeric character, stop.
            if (foundDigit) break;
        }
    }

    if (!result.empty() && TypeChecker::isNumeric(result)) {
        return result;
    }

    return input; // Return original if no valid number was extracted
}