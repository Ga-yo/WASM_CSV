#include "csv_utils.h"
#include "type_checker.h"

using namespace std;

// 문자열 앞뒤 공백 제거
string trim(const string& str) {
    const char* whitespace = " \t\r\n"; // 스페이스, 탭, 캐리지 리턴, 개행
    size_t first = str.find_first_not_of(whitespace);
    if (first == string::npos) return "";
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, last - first + 1);
}

// UTF-8 BOM 제거
string removeBOM(const string& str) {
    if (str.length() >= 3 &&
        (unsigned char)str[0] == 0xEF &&
        (unsigned char)str[1] == 0xBB &&
        (unsigned char)str[2] == 0xBF) {
        return str.substr(3);
    }
    return str;
}

// 윈도우(\r\n)나 구형 Mac(\r)의 줄바꿈 문자를 유닉스/리눅스 스타일(\n)로 통일
string normalizeLineEndings(const string& str) {
    string result;
    result.reserve(str.size()); // 미리 메모리를 할당하여 성능을 최적화합니다.
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '\r') {
            // \r\n (윈도우) -> \n
            if (i + 1 < str.length() && str[i + 1] == '\n') {
                result += '\n';
                i++; // \n은 이미 처리했으므로 건너뜁니다.
            } else {
                // \r (구형 Mac) -> \n
                result += '\n';
            }
        } else {
            result += str[i];
        }
    }
    return result;
}

// 문자열을 JSON 형식에 맞게 이스케이프 처리합니다. (예: " -> \")
string escapeJson(const string& str) {
    string result;
    // 빠른 경로 최적화: 이스케이프할 특수 문자가 없으면 원본 문자열을 바로 반환합니다.
    size_t first_special = str.find_first_of("\"\\\b\f\n\r\t");
    if (first_special == string::npos) {
        return str;
    }

    // 이스케이프 문자가 추가될 것을 대비해 약간 더 많은 메모리를 예약합니다.
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

// 숫자처럼 보이는 문자열에서 숫자 부분만 추출
// 예: " 1,234.56 원" -> "1234.56"
string cleanNumericString(const string& input) {
    string result;
    result.reserve(input.length()); // 최적화를 위해 메모리 예약
    bool foundDigit = false;   // 숫자를 찾았는지 여부
    bool foundDecimal = false; // 소수점을 찾았는지 여부
    bool foundSign = false;    // 부호(+, -)를 찾았는지 여부

    // 문자열 앞 공백이 있다면 무시
    size_t start = input.find_first_not_of(" \t");
    if (start == string::npos) return "";

    for (size_t i = start; i < input.length(); ++i) {
        char c = input[i];

        if (isdigit(c)) {
            // 숫자인 경우 결과에 추가
            result += c;
            foundDigit = true;
        } else if (c == '.' && !foundDecimal) {
            // 첫 번째 소수점인 경우 결과에 추가
            if (result.empty()) result += '0';
            result += c;
            foundDecimal = true;
        } else if ((c == '+' || c == '-') && !foundDigit && !foundSign) {
            // 숫자나 다른 부호가 나오기 전의 첫 번째 부호인 경우 결과에 추가
            result += c;
            foundSign = true;
        } else if (c == ',' || c == ' ') {
            // 쉼표(,)나 공백은 무시 (천 단위 구분자 등)
            if (foundDigit) continue;
        } else {
            // 숫자를 찾은 이후에 숫자와 관련 없는 문자가 나오면 중단
            if (foundDigit) break;
        }
    }

    // 정리된 문자열이 유효한 숫자인 경우에만 반환
    if (!result.empty() && TypeChecker::isNumeric(result)) {
        return result;
    }

    // 유효한 숫자를 추출하지 못했다면 원본 문자열을 반환
    return input; 
}