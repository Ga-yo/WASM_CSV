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

string cleanNumericString(const string& input) {
    if (input.empty()) return input;

    // 빠른 경로 1: 이미 순수한 숫자인 경우 (가장 흔한 케이스)
    char first = input[0];
    if (isdigit(first) || first == '-' || first == '+' || first == '.') {
        bool needs_cleaning = false;
        for (char c : input) {
            if (c == ',' || c == ' ') {
                needs_cleaning = true;
                break;
            }
            if (!isdigit(c) && c != '.' && c != '-' && c != '+' && c != 'e' && c != 'E') {
                needs_cleaning = true;
                break;
            }
        }

        if (needs_cleaning) {
            string result;
            result.reserve(input.size());
            for (char c : input) {
                if (c != ',' && c != ' ') {
                    result += c;
                }
            }
            if (!result.empty() && TypeChecker::isNumeric(result)) {
                return result;
            }
        } else {
            return input;
        }
    }

    // 빠른 경로 2: 숫자가 전혀 없으면 원본 반환
    if (input.find_first_of("0123456789") == string::npos) {
        return input;
    }

    // 느린 경로: 정규식 사용 (통화 기호 등이 포함된 경우)
    static const regex num_regex(R"([+-]?\s*[\d,]+(?:\.\d+)?)");
    smatch match;

    if (regex_search(input, match, num_regex) && !match.empty()) {
        string extracted_num = match[0].str();
        string result;
        result.reserve(extracted_num.size());
        for (char c : extracted_num) {
            if (c != ',' && c != ' ') result += c;
        }
        if (!result.empty() && TypeChecker::isNumeric(result)) return result;
    }

    return input; // 숫자 부분을 찾지 못하면 원본 반환
}