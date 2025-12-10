#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <iomanip>

#include "csv_types.h"
#include "type_checker.h"
#include "csv_utils.h"
#include "csv_parser.h"
#include "csv_converter.h"

using namespace std;
using namespace emscripten;

// 데이터 타입 열거형을 문자열로 변환하는 헬퍼 함수
static string dataTypeToString(DataType type) {
    switch (type) {
        case DataType::INTEGER: return "integer";
        case DataType::FLOAT:   return "float";
        case DataType::BOOLEAN: return "boolean";
        case DataType::DATE:    return "date";
        case DataType::STRING:  return "string";
    }
    return "string";
}

// 숫자 통계(합계, 평균, 분산 등)를 갱신하는 함수 (Welford's algorithm 사용)
static void addNumericValue(ColumnStats& stats, double value) {
    stats.count++;
    stats.sum += value;

    if (stats.count == 1) {
        stats.min = stats.max = value;
        stats.mean = value;
        stats.m2 = 0;
    } else {
        stats.min = std::min(stats.min, value);
        stats.max = std::max(stats.max, value);

        double delta = value - stats.mean;
        stats.mean += delta / stats.count;
        double delta2 = value - stats.mean;
        stats.m2 += delta * delta2;
    }
}

// 표준 편차 계산 함수
static double getStdDev(const ColumnStats& stats) {
    return stats.count > 1 ? sqrt(stats.m2 / (stats.count - 1)) : NAN;
}

// 주어진 값들의 샘플을 기반으로 컬럼의 데이터 타입을 추론하는 함수
static DataType detectColumnType(const vector<string>& values) {
    bool allInteger = true;
    bool allFloat = true;
    bool allBoolean = true;
    bool allDate = true;
    int nonNullCount = 0;

    for (const auto& val : values) {
        if (TypeChecker::isNull(val)) continue;
        nonNullCount++;

        // 숫자형 판별을 위해 문자열 정제 (예: "1,000" -> "1000")
        string cleanedVal = cleanNumericString(val);
        if (!TypeChecker::isNumeric(cleanedVal)) {
             allInteger = false;
             allFloat = false;
        } else {
            // 정수인지 실수인지 구체적으로 확인
            if (allInteger && !TypeChecker::isInteger(cleanedVal)) allInteger = false;
            if (allFloat && !TypeChecker::isFloat(cleanedVal) && !TypeChecker::isInteger(cleanedVal)) allFloat = false;
        }

        if (allBoolean && !TypeChecker::isBoolean(val)) allBoolean = false;
        if (allDate && !TypeChecker::isDate(val)) allDate = false;

        if (!allInteger && !allFloat && !allBoolean && !allDate) break;
    }

    // 우선순위에 따라 타입 결정 (Boolean > Date > Integer > Float > String)
    if (nonNullCount == 0) return DataType::STRING;
    if (allBoolean) return DataType::BOOLEAN;
    if (allDate) return DataType::DATE;
    if (allInteger) return DataType::INTEGER;
    if (allFloat) return DataType::FLOAT;
    return DataType::STRING;
}

// JSON 출력 시 double 값을 안전하게 처리하는 함수 (NaN, Inf 처리 및 소수점 자릿수)
static void jsonSafeDouble(ostringstream& json, double value) {
    json << setprecision(16) << defaultfloat; 
    
    if (isfinite(value)) {
        json << value; 
    } else {
        json << "null"; 
    }

    json << fixed << setprecision(2); 
}

// CSV 내용을 최적화된 방식으로 JSON으로 변환하는 메인 함수
string convertToJsonOptimized(const string& csvContent, const string& filename) {
    // BOM 제거 및 줄바꿈 정규화
    string content = removeBOM(csvContent);
    content = normalizeLineEndings(content);

    // CSV 파싱 실행
    CSVParseResult parsed = parseCSV(content); 

    vector<string>& headers = parsed.headers;
    vector<vector<string>>& rows = parsed.rows;
    char delimiter = parsed.delimiter;
    
    if (headers.empty()) {
        return "{\"error\":\"Empty CSV\",\"metadata\":{\"filename\":\"" + escapeJson(filename) + "\"}}";
    }

    const int numColumns = headers.size();
    int numRows = rows.size();

    for (auto& row : rows) {
        row.resize(numColumns);
    }

    // 통계 및 타입 감지를 위한 변수 초기화
    vector<DataType> columnTypes(numColumns);
    vector<ColumnStats> stats(numColumns);
    vector<unordered_set<size_t>> uniqueValHashes(numColumns);
    std::hash<string> stringHasher;

    for (int i = 0; i < numColumns; i++) {
        uniqueValHashes[i].reserve(::min(numRows, 10000));
    }

    // 1. 샘플링: 데이터 타입 감지를 위해 최대 1000행까지 샘플링
    int sampleSize = ::min(numRows, 1000);
    vector<vector<string>> sampleData(numColumns);
    for (int i = 0; i < numColumns; i++) {
        sampleData[i].reserve(sampleSize);
    }

    for (int r = 0; r < sampleSize && r < numRows; r++) {
        for (int c = 0; c < numColumns; c++) {
            sampleData[c].push_back(rows[r][c]);
        }
    }

    // 2. 타입 감지: 샘플 데이터를 바탕으로 각 컬럼의 타입 결정
    for (int i = 0; i < numColumns; i++) {
        columnTypes[i] = detectColumnType(sampleData[i]);
        stats[i].type = columnTypes[i];
    }

    // 3. 전체 데이터 순회: 데이터 정제 및 통계 계산
    for (int r = 0; r < numRows; r++) {
        for (int c = 0; c < numColumns; c++) {
            const string& originalVal = rows[r][c];
            string val = originalVal;

            // 숫자 타입인 경우 문자열 정제 (예: "1,000" -> "1000")
            if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                rows[r][c] = cleanNumericString(originalVal); 
                val = rows[r][c]; 
            }

            // NULL 체크 및 카운트
            if (TypeChecker::isNull(val)) {
                stats[c].nullCount++;
                continue;
            }

            // 고유값 해시 저장 (메모리 보호를 위해 최대 개수 제한)
            if (uniqueValHashes[c].size() < 50000) {
                uniqueValHashes[c].insert(stringHasher(val));
            }

            // 타입별 통계 갱신
            if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                double num = std::stod(val); // Use std::stod as it's already verified numeric
                if (!isnan(num)) {
                    addNumericValue(stats[c], num);
                }
            } else if (columnTypes[c] == DataType::STRING) {
                uint32_t len = val.length();
                stats[c].minLength = ::min(stats[c].minLength, len);
                stats[c].maxLength = ::max(stats[c].maxLength, len);
            }
        }
    }

    // 최종 통계 정리 (고유값 개수 등)
    for (int i = 0; i < numColumns; i++) {
        stats[i].uniqueCount = uniqueValHashes[i].size();
        if (stats[i].minLength == UINT32_MAX) stats[i].minLength = 0;
    }

    ostringstream json;
    json << fixed << setprecision(2);

    // 메모리 할당 최적화 (예상 크기 예약)
    size_t estimatedSize = content.length() * 1.5 + (numRows * numColumns * 20);
    json.str().reserve(estimatedSize);

    // 메타데이터 작성
    json << "{\"metadata\":{\"filename\":\"" << escapeJson(filename) << "\"";
    json << ",\"totalRows\":" << numRows;
    json << ",\"totalColumns\":" << numColumns;
    json << ",\"fileSizeBytes\":" << content.length();
    json << ",\"columns\":[";

    // 컬럼 정보 및 통계 작성
    for (int i = 0; i < numColumns; i++) {
        if (i > 0) json << ",";
        json << "{\"name\":\"" << escapeJson(headers[i]) << "\"";
        json << ",\"type\":\"" << dataTypeToString(columnTypes[i]) << "\"";
        json << ",\"stats\":{\"count\":" << (numRows - stats[i].nullCount);
        json << ",\"unique\":" << stats[i].uniqueCount;
        json << ",\"nullCount\":" << stats[i].nullCount;

        if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
            if (stats[i].count > 0) {
                json << ",\"min\":"; jsonSafeDouble(json, stats[i].min);
                json << ",\"max\":"; jsonSafeDouble(json, stats[i].max);
                json << ",\"avg\":"; jsonSafeDouble(json, stats[i].mean);
                json << ",\"std_dev\":"; jsonSafeDouble(json, getStdDev(stats[i]));
            }
        } else if (columnTypes[i] == DataType::STRING) {
            json << ",\"min_length\":" << stats[i].minLength;
            json << ",\"max_length\":" << stats[i].maxLength;
        }
        json << "}}";
    }

    json << "]},\"data\":[";

    // 헤더 이스케이프 미리 처리
    vector<string> escapedHeaders(numColumns);
    for (int i = 0; i < numColumns; i++) {
        escapedHeaders[i] = escapeJson(headers[i]);
    }

    // 실제 데이터 배열 작성
    for (int r = 0; r < numRows; r++) {
        if (r > 0) json << ",";
        json << "{";
        for (int c = 0; c < numColumns; c++) {
            if (c > 0) json << ",";
            json << "\"" << escapedHeaders[c] << "\":";

            const string& val = rows[r][c];
            // 타입별 값 처리 (Null, Number, Boolean, String)
            if (TypeChecker::isNull(val)) {
                json << "null";
            } else if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                double num = std::stod(val);
                jsonSafeDouble(json, num);
            } else if (columnTypes[c] == DataType::BOOLEAN) {
                char first = val.empty() ? '\0' : val[0];
                if (first == 't' || first == 'T' || first == 'y' || first == 'Y' || first == '1') {
                    json << "true";
                } else if (first == 'f' || first == 'F' || first == 'n' || first == 'N' || first == '0') {
                    json << "false";
                } else {
                    json << "\"" << escapeJson(val) << "\"";
                }
            } else {
                json << "\"" << escapeJson(val) << "\"";
            }
        }
        json << "}";
    }

    json << "]}";
    return json.str();
}