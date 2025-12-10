#ifndef CSV_TYPES_H
#define CSV_TYPES_H

#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

// 데이터 타입 열거형

enum class DataType {
    INTEGER,
    FLOAT,
    BOOLEAN,
    DATE,
    STRING
};

// CSV 파싱 결과 구조체

struct CSVParseResult {
    std::vector<std::string> headers;           // 헤더(컬럼 이름) 목록
    std::vector<std::vector<std::string>> rows; // 실제 데이터 행들 (2차원 벡터)
    char delimiter;                             // 감지된 구분자 (예: ',', '\t')
};

// 컬럼 통계 구조체

struct ColumnStats {
    double min = NAN;                   // 최소값 (Not-a-Number로 초기화)
    double max = NAN;                   // 최대값
    double sum = 0;                     // 합계
    double mean = NAN;                  // 평균
    double m2 = 0;                      // 표준 편차 계산을 위한 중간 값 (Welford's algorithm)

    DataType type = DataType::STRING;   // 컬럼의 데이터 타입
    uint32_t nullCount = 0;             // NULL 값의 개수
    uint32_t uniqueCount = 0;           // 고유한 값의 개수
    uint32_t count = 0;                 // NULL이 아닌 값의 개수 (숫자 통계용)
    uint32_t minLength = UINT32_MAX;    // 문자열의 최소 길이 (첫 비교를 위해 최대값으로 초기화)
    uint32_t maxLength = 0;             // 문자열의 최대 길이
};

#endif // CSV_TYPES_H