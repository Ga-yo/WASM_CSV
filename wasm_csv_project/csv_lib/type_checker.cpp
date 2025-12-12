#include "type_checker.h"
#include <algorithm>
#include <cmath>
#include <cerrno>

using namespace std;

// 문자열 -> double 변환 시도 함수(파일 내부에서만 사용)
static double stringToDouble(const string& input) {
    if (input.empty()) return NAN; // 빈 문자열은 숫자가 아님

    const char* start_ptr = input.c_str(); // 변환을 시작할 위치
    char* end_ptr = nullptr; // 변환이 끝난 위치를 저장할 포인터
    errno = 0;

    // 문자열 -> double 변환 시도
    double converted_value = strtod(start_ptr, &end_ptr);

    // 오류 발생 가능 1: 변환이 전혀 이루어지지 않음 (예: "abc")
    const bool no_conversion_made = (end_ptr == start_ptr); 
    // 오류 발생 가능 2: 숫자 뒤에 추가 문자가 있음 (예: "123a")
    const bool has_trailing_chars = (*end_ptr != '\0');
    // 오류 발생 가능 3: 변환된 값이 double의 표현 범위를 벗어남
    const bool is_out_of_range = (errno == ERANGE);

    if (no_conversion_made || has_trailing_chars || is_out_of_range) {
        return NAN;
    }
    return converted_value;
}

// 문자열 비교(대소문자 구분 X)
inline bool iequals(const string& a, const string& b) {
    // a와 b를 처음부터 끝까지 각 문자를 소문자로 변환하면서 비교
    return equal(a.begin(), a.end(),
                 b.begin(), b.end(),
                 [](char a, char b) { // 람다함수로 소문자로 변환함
                     return tolower(a) == tolower(b);
                 });
}

// 정수판별
bool TypeChecker::isInteger(const string& str) {
    double num = stringToDouble(str);
    return !isnan(num) && trunc(num) == num;
}

// 실수판별
bool TypeChecker::isFloat(const string& str) {
    double num = stringToDouble(str);
    return !isnan(num) && trunc(num) != num;
}

// 정수 및 실수 (숫자) 판별
bool TypeChecker::isNumeric(const string& str) {
    return !isnan(stringToDouble(str));
}

// 불리언 판별
bool TypeChecker::isBoolean(const string& str) {
    // 최적화: "true", "false", "yes", "no" 등은 5글자 이하
    if (str.length() > 5) return false; 
    return iequals(str, "true") || iequals(str, "false") ||
           iequals(str, "yes") || iequals(str, "no") ||
           str == "1" || str == "0";
}

// 날짜 판별
bool TypeChecker::isDate(const string& str) {
    // YYYYMMDD(8) ~ YYYY-MM-DD(10) 형식만 간단히 검사
    if (str.length() < 8 || str.length() > 10) return false; 
    if (str.length() == 10) {
        return (str[4] == '-' || str[4] == '/') && (str[7] == '-' || str[7] == '/');
    }
    return false;
}

// NULL 판별
bool TypeChecker::isNull(const string& str) {
    // 빈 문자열은 NULL
    if (str.empty()) return true; 
    // 최적화: "null", "n/a" 등은 4글자 이하
    if (str.length() > 4) return false; 
    return iequals(str, "null") ||
           iequals(str, "na") ||
           iequals(str, "n/a") ||
           iequals(str, "nan") ||
           str == "-"; 
}