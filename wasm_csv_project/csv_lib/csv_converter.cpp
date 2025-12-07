#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <iomanip>
#include <regex>
#include <ctime>
#include <cerrno>

#include "csv_types.h"
#include "type_checker.h"
#include "csv_utils.h"
#include "csv_parser.h"
#include "csv_converter.h"

using namespace std;
using namespace emscripten;

// =========================
// Column Statistics
// =========================

static DataType detectColumnType(const vector<string>& values) {
    bool allInteger = true;
    bool allFloat = true;
    bool allBoolean = true;
    bool allDate = true;
    int nonNullCount = 0;

    for (const auto& val : values) {
        if (TypeChecker::isNull(val)) continue;
        nonNullCount++;

        string cleanedVal = cleanNumericString(val);
        if (!TypeChecker::isNumeric(cleanedVal)) {
             allInteger = false;
             allFloat = false;
        } else {
            if (allInteger && !TypeChecker::isInteger(cleanedVal)) allInteger = false;
            if (allFloat && !TypeChecker::isFloat(cleanedVal) && !TypeChecker::isInteger(cleanedVal)) allFloat = false;
        }

        if (allBoolean && !TypeChecker::isBoolean(val)) allBoolean = false;
        if (allDate && !TypeChecker::isDate(val)) allDate = false;

        if (!allInteger && !allFloat && !allBoolean && !allDate) break;
    }

    if (nonNullCount == 0) return DataType::STRING;
    if (allBoolean) return DataType::BOOLEAN;
    if (allDate) return DataType::DATE;
    if (allInteger) return DataType::INTEGER;
    if (allFloat) return DataType::FLOAT;
    return DataType::STRING;
}


// ---------------------------------------------------------------------------------

// =================================================================================
// Main Optimized Conversion Function 
// =================================================================================

inline void jsonSafeDouble(ostringstream& json, double value) {
    json << setprecision(16) << defaultfloat; 
    
    if (isfinite(value)) {
        json << value; 
    } else {
        json << "null"; 
    }

    json << fixed << setprecision(2); 
}

// 💡 Reverted to 2-argument signature
string convertToJsonOptimized(const string& csvContent, const string& filename) {
    // 1. Preprocess
    string content = removeBOM(csvContent);
    content = normalizeLineEndings(content);

    // 2. Standard Single-Header Parsing
    CSVParseResult parsed = parseCSV(content); 

    vector<string>& headers = parsed.headers;
    vector<vector<string>>& rows = parsed.rows;
    char delimiter = parsed.delimiter;
    
    if (headers.empty()) {
        return "{\"error\":\"Empty CSV\",\"metadata\":{\"filename\":\"" + escapeJson(filename) + "\"}}";
    }

    const int numColumns = headers.size();
    int numRows = rows.size();

    // 3. Normalize row lengths only (cleanup이 필요한 경우에만 수행)
    for (auto& row : rows) {
        row.resize(numColumns);
    }

    // 4. 타입 감지 및 통계 수집 - 전치 없이 직접 처리 (메모리 최적화)
    vector<DataType> columnTypes(numColumns);
    vector<ColumnStats> stats(numColumns);
    vector<unordered_set<string>> uniqueVals(numColumns);

    // 각 컬럼별 unique 값 저장소 예약
    for (int i = 0; i < numColumns; i++) {
        uniqueVals[i].reserve(::min(numRows, 10000));
    }

    // 타입 감지용 샘플 데이터 수집 (첫 1000행)
    int sampleSize = ::min(numRows, 1000);
    vector<vector<string>> sampleData(numColumns);
    for (int i = 0; i < numColumns; i++) {
        sampleData[i].reserve(sampleSize);
    }

    // 첫 패스: 샘플링 및 타입 감지 (cleanNumericString 호출 최소화)
    for (int r = 0; r < sampleSize && r < numRows; r++) {
        for (int c = 0; c < numColumns; c++) {
            sampleData[c].push_back(rows[r][c]);
        }
    }

    // 타입 감지
    for (int i = 0; i < numColumns; i++) {
        columnTypes[i] = detectColumnType(sampleData[i]);
        stats[i].type = columnTypes[i];
    }

    // 두 번째 패스: 통계 수집 (숫자 컬럼만 cleanNumericString 호출)
    for (int r = 0; r < numRows; r++) {
        for (int c = 0; c < numColumns; c++) {
            // Use reference to avoid copying
            const string& originalVal = rows[r][c];
            string val = originalVal;

            // 숫자 타입 컬럼만 정리 수행 (대폭 성능 향상)
            if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                rows[r][c] = cleanNumericString(originalVal); // 정리된 값으로 교체
                val = rows[r][c]; // Use the cleaned value for stats
            }

            if (TypeChecker::isNull(val)) {
                stats[c].nullCount++;
                continue;
            }

            // Unique 값 추적 (메모리 제한)
            if (uniqueVals[c].size() < 50000) {
                uniqueVals[c].insert(val);
            }

            // 타입별 통계
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

    // Unique count 설정
    for (int i = 0; i < numColumns; i++) {
        stats[i].uniqueCount = uniqueVals[i].size();
        if (stats[i].minLength == UINT32_MAX) stats[i].minLength = 0;
    }


    // 5. Build JSON (메모리 예약으로 재할당 최소화)
    ostringstream json;
    json << fixed << setprecision(2);

    // 예상 JSON 크기 계산하여 버퍼 예약 (재할당 최소화)
    size_t estimatedSize = content.length() * 1.5 + (numRows * numColumns * 20);
    json.str().reserve(estimatedSize);

    json << "{\"metadata\":{\"filename\":\"" << escapeJson(filename) << "\"";
    json << ",\"totalRows\":" << numRows;
    json << ",\"totalColumns\":" << numColumns;
    json << ",\"fileSizeBytes\":" << content.length();
    json << ",\"columns\":[";

    for (int i = 0; i < numColumns; i++) {
        if (i > 0) json << ",";
        json << "{\"name\":\"" << escapeJson(headers[i]) << "\"";
        json << ",\"type\":\"" << dataTypeToString(columnTypes[i]) << "\"";
        json << ",\"stats\":{\"count\":" << (numRows - stats[i].nullCount);
        json << ",\"unique\":" << stats[i].uniqueCount;
        json << ",\"nullCount\":" << stats[i].nullCount;

        if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
            if (stats[i].count > 0) { // 방어 코드: 숫자 데이터가 하나라도 있을 때만 통계 출력
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

    // Data output - 헤더 이스케이프 캐싱
    vector<string> escapedHeaders(numColumns);
    for (int i = 0; i < numColumns; i++) {
        escapedHeaders[i] = escapeJson(headers[i]);
    }

    // Data output
    for (int r = 0; r < numRows; r++) {
        if (r > 0) json << ",";
        json << "{";
        for (int c = 0; c < numColumns; c++) {
            if (c > 0) json << ",";
            json << "\"" << escapedHeaders[c] << "\":";

            const string& val = rows[r][c];
            if (TypeChecker::isNull(val)) {
                json << "null";
            } else if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                double num = std::stod(val);
                jsonSafeDouble(json, num);
            } else if (columnTypes[c] == DataType::BOOLEAN) {
                // 최적화: 대소문자 변환 없이 첫 글자만 체크
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

// =================================================================================
// Placeholder/Wrapper functions (for API compatibility)
// =================================================================================

// 💡 Reverted to 2-argument signatures for API compatibility
string convertToJson(const string& csvContent, const string& filename) {
    return convertToJsonOptimized(csvContent, filename);
}

string convertToJsonMetadataOnly(const string& csvContent, const string& filename) {
    return convertToJsonOptimized(csvContent, filename);
}

string convertToJsonAuto(const string& csvContent, const string& filename) {
    return convertToJsonOptimized(csvContent, filename);
}