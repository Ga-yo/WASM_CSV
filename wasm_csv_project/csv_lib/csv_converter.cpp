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
#include <cstdlib>
#include <thread>
#include <future>
#include <wasm_simd128.h>
#include <cstring>

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
        stats.min = min(stats.min, value);
        stats.max = max(stats.max, value);

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
    bool isColumnInteger = true;
    bool isColumnFloat = true;
    bool isColumnBoolean = true;
    bool isColumnDate = true;
    int nonNullCount = 0;

    for (const auto& val : values) {
        if (TypeChecker::isNull(val)) continue;
        nonNullCount++;

        // 숫자형 판별을 위해 문자열 정제 (예: "1,000" -> "1000")
        string cleanedVal = cleanNumericString(val);
        if (!TypeChecker::isNumeric(cleanedVal)) {
             isColumnInteger = false;
             isColumnFloat = false;
        } else {
            // 정수인지 실수인지 구체적으로 확인
            if (isColumnInteger && !TypeChecker::isInteger(cleanedVal)) isColumnInteger = false;
            if (isColumnFloat && !TypeChecker::isFloat(cleanedVal) && !TypeChecker::isInteger(cleanedVal)) isColumnFloat = false;
        }

        if (isColumnBoolean && !TypeChecker::isBoolean(val)) isColumnBoolean = false;
        if (isColumnDate && !TypeChecker::isDate(val)) isColumnDate = false;

        if (!isColumnInteger && !isColumnFloat && !isColumnBoolean && !isColumnDate) break;
    }

    // 우선순위에 따라 타입 결정 (Boolean > Date > Integer > Float > String)
    if (nonNullCount == 0) return DataType::STRING;
    if (isColumnBoolean) return DataType::BOOLEAN;
    if (isColumnDate) return DataType::DATE;
    if (isColumnInteger) return DataType::INTEGER;
    if (isColumnFloat) return DataType::FLOAT;
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
    hash<string> stringHasher;

    for (int i = 0; i < numColumns; i++) {
        uniqueValHashes[i].reserve(min(numRows, 10000));
    }

    // 1. 샘플링: 데이터 타입 감지를 위해 최대 1000행까지 샘플링
    int sampleSize = min(numRows, 1000);
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
                double num = stod(val);
                if (!isnan(num)) {
                    addNumericValue(stats[c], num);
                }
            } else if (columnTypes[c] == DataType::STRING) {
                uint32_t len = val.length();
                stats[c].minLength = min(stats[c].minLength, len);
                stats[c].maxLength = max(stats[c].maxLength, len);
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
                double num = stod(val);
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

// ============================================================================
// Zero-Copy 방식의 새로운 파서 클래스
// ============================================================================

// 숫자 파싱 최적화 헬퍼 (메모리 할당 없음)
static double fastParseDouble(const char* start, const char* end) {
    // 1. 공백 건너뛰기
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    if (start >= end) return NAN;

    // 2. NULL/NaN 체크 (빠른 비교)
    size_t len = end - start;
    if (len <= 4) {
        if ((len == 4 && strncasecmp(start, "null", 4) == 0) ||
            (len == 3 && strncasecmp(start, "nan", 3) == 0) ||
            (len == 2 && strncasecmp(start, "na", 2) == 0)) {
            return NAN;
        }
    }

    double val = 0.0;
    double sign = 1.0;
    
    if (*start == '-') { sign = -1.0; start++; }
    else if (*start == '+') { start++; }

    bool hasDot = false;
    double divisor = 1.0;
    bool hasDigit = false;

    for (; start < end; start++) {
        char c = *start;
        if (c >= '0' && c <= '9') {
            hasDigit = true;
            if (hasDot) {
                divisor *= 10.0;
                val = val + (c - '0') / divisor;
            } else {
                val = val * 10.0 + (c - '0');
            }
        } else if (c == '.') {
            if (hasDot) break; 
            hasDot = true;
        } else if (c == ',' || c == ' ' || c == '\t' || c == '"') {
            // 무시할 문자들 (쉼표, 공백, 따옴표)
            continue;
        } else {
            // 숫자와 관련 없는 문자 발견 시 중단
            break;
        }
    }
    
    return hasDigit ? val * sign : NAN;
}

// 스레드별 파싱 결과를 저장할 구조체
struct ThreadResult {
    vector<vector<double>> numericData;
    vector<vector<string>> stringData;
    vector<ColumnStats> stats;
    int rowCount = 0;
};

class WasmCSVParser {
private:
    vector<string> headers;
    vector<DataType> columnTypes;
    vector<vector<double>> numericData;      // 숫자형 컬럼 데이터 (메모리 공유용)
    vector<vector<string>> stringData;       // 문자열형 컬럼 데이터
    vector<ColumnStats> stats;
    int rowCount = 0;

    // 빠른 파싱을 위한 내부 상태
    const char* cursor = nullptr;
    const char* end = nullptr;

    // Packed String Transfer Support
    vector<uint8_t> packedData;
    vector<uint32_t> packedOffsets;

public:
    WasmCSVParser() {}

    void parse(const string& csvContent) {
        parseBuffer(csvContent.c_str(), csvContent.length());
    }

    void parseBytes(size_t ptr, size_t length) {
        parseBuffer(reinterpret_cast<const char*>(ptr), length);
    }

    void parseBuffer(const char* startPtr, size_t length) {
        // 초기화
        headers.clear();
        columnTypes.clear();
        numericData.clear();
        stringData.clear();
        stats.clear();
        rowCount = 0;

        cursor = startPtr;
        end = cursor + length;

        // 1. BOM 처리 (복사 없이 포인터 이동만)
        if (length >= 3 && (unsigned char)cursor[0] == 0xEF && (unsigned char)cursor[1] == 0xBB && (unsigned char)cursor[2] == 0xBF) {
            cursor += 3;
        }

        // 2. 헤더 파싱
        if (cursor < end) {
            parseLine(headers);
        }
        
        int numColumns = headers.size();
        if (numColumns == 0) return;

        // 3. 타입 감지를 위한 샘플링 (처음 1000행)
        // 전체를 문자열로 저장하지 않고, 샘플만 임시 저장
        int sampleLimit = 1000;
        vector<vector<string>> sampleRows;
        sampleRows.reserve(sampleLimit);

        const char* sampleStartCursor = cursor; // 샘플링 후 다시 돌아오기 위해 위치 저장
        
        while (cursor < end && sampleRows.size() < sampleLimit) {
            vector<string> row;
            row.reserve(numColumns);
            if (parseLine(row)) {
                // 열 개수가 맞지 않는 행은 보정
                while (row.size() < numColumns) row.push_back("");
                if (row.size() > numColumns) row.resize(numColumns);
                sampleRows.push_back(move(row));
            }
        }

        // 4. 타입 감지
        columnTypes.resize(numColumns, DataType::STRING);
        stats.resize(numColumns);
        
        for (int c = 0; c < numColumns; c++) {
            vector<string> sample;
            sample.reserve(sampleRows.size());
            for (const auto& row : sampleRows) {
                sample.push_back(row[c]);
            }
            columnTypes[c] = detectColumnType(sample);
            stats[c].type = columnTypes[c];
        }

        // 5. 데이터 저장소 초기화 및 메모리 예약
        // 전체 행 개수 추정 (파일 크기 기반)
        size_t estimatedRows = length / (numColumns * 10); // 대략적인 평균 행 길이 가정
        if (estimatedRows < sampleRows.size()) estimatedRows = sampleRows.size() * 2;

        numericData.resize(numColumns);
        stringData.resize(numColumns);

        for (int c = 0; c < numColumns; c++) {
            if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                numericData[c].reserve(estimatedRows);
            } else {
                stringData[c].reserve(estimatedRows);
            }
        }

        // 6. 샘플 데이터 처리
        for (const auto& row : sampleRows) {
            processRow(row);
        }

        // 7. 나머지 데이터 파싱 (멀티스레딩 적용)
        // cursor는 이미 샘플링 이후 위치에 있음
        size_t remainingLen = end - cursor;
        
        // 데이터가 작으면 싱글 스레드로 처리 (오버헤드 방지)
        if (remainingLen < 1024 * 1024) { // 1MB 미만
            ThreadResult res;
            initThreadResult(res, numColumns, estimatedRows);
            parseChunk(cursor, end, res);
            mergeThreadResult(res);
        } else {
            // 멀티스레드 병렬 처리
            int numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4;
            // 브라우저 환경 고려하여 최대 스레드 제한 (메인 스레드 제외)
            numThreads = min(numThreads, 8); 

            vector<std::thread> threads;
            vector<ThreadResult> results(numThreads);
            
            const char* chunkStart = cursor;
            size_t chunkSize = remainingLen / numThreads;

            for (int i = 0; i < numThreads; i++) {
                const char* chunkEnd;
                if (i == numThreads - 1) {
                    chunkEnd = end;
                } else {
                    chunkEnd = chunkStart + chunkSize;
                    // 줄바꿈 단위로 청크 경계 조정
                    while (chunkEnd < end && *chunkEnd != '\n') chunkEnd++;
                    if (chunkEnd < end) chunkEnd++; // 개행 문자 포함
                }

                initThreadResult(results[i], numColumns, estimatedRows / numThreads);
                
                // 스레드 실행
                threads.emplace_back(&WasmCSVParser::parseChunk, this, chunkStart, chunkEnd, std::ref(results[i]));
                
                chunkStart = chunkEnd;
                if (chunkStart >= end) break;
            }

            // 스레드 종료 대기 및 결과 병합
            for (auto& t : threads) {
                if (t.joinable()) t.join();
            }

            for (auto& res : results) {
                mergeThreadResult(res);
            }
        }
    }

private:
    void initThreadResult(ThreadResult& res, int numCols, size_t estRows) {
        res.numericData.resize(numCols);
        res.stringData.resize(numCols);
        res.stats.resize(numCols);
        for (int i = 0; i < numCols; i++) {
            res.stats[i].type = columnTypes[i];
            if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
                res.numericData[i].reserve(estRows);
            } else {
                res.stringData[i].reserve(estRows);
            }
        }
    }

    // 청크 단위 파싱 (스레드에서 실행)
    void parseChunk(const char* start, const char* chunkEnd, ThreadResult& res) {
        int colIndex = 0;
        const char* curr = start;
        const char* tokenStart = curr;
        bool inQuotes = false;
        bool hasEscapedQuotes = false;

        // SIMD 상수 벡터 (16바이트씩 비교하기 위해 미리 생성)
        const v128_t quoteVec = wasm_i8x16_splat('"');
        const v128_t commaVec = wasm_i8x16_splat(',');
        const v128_t crVec = wasm_i8x16_splat('\r');
        const v128_t lfVec = wasm_i8x16_splat('\n');

        while (curr < chunkEnd) {
            // SIMD 가속: 특수 문자가 없는 구간을 16바이트씩 건너뜀
            // 남은 데이터가 16바이트 이상일 때만 수행
            if (curr + 16 <= chunkEnd) {
                v128_t data = wasm_v128_load((const v128_t*)curr);
                int mask = 0;
                
                if (!inQuotes) {
                    // 따옴표 밖: ", \r, \n, , 중 하나라도 있는지 병렬 체크
                    v128_t eqQuote = wasm_i8x16_eq(data, quoteVec);
                    v128_t eqComma = wasm_i8x16_eq(data, commaVec);
                    v128_t eqCr = wasm_i8x16_eq(data, crVec);
                    v128_t eqLf = wasm_i8x16_eq(data, lfVec);
                    
                    v128_t anySpecial = wasm_v128_or(wasm_v128_or(eqQuote, eqComma), wasm_v128_or(eqCr, eqLf));
                    mask = wasm_i8x16_bitmask(anySpecial);
                } else {
                    // 따옴표 안: " 만 체크
                    v128_t eqQuote = wasm_i8x16_eq(data, quoteVec);
                    mask = wasm_i8x16_bitmask(eqQuote);
                }

                if (mask == 0) {
                    curr += 16; // 특수 문자가 없으면 16바이트 점프
                    continue;
                } else {
                    // 특수 문자가 발견된 경우, 해당 위치 바로 앞까지 점프
                    int trailing_zeros = __builtin_ctz(mask);
                    curr += trailing_zeros;
                }
            }

            char c = *curr;
            
            if (c == '"') {
                if (inQuotes && curr + 1 < chunkEnd && *(curr + 1) == '"') {
                    // 이스케이프된 따옴표 ("")
                    hasEscapedQuotes = true;
                    curr++; // 다음 따옴표 건너뛰기
                } else {
                    inQuotes = !inQuotes;
                }
            } else if (!inQuotes) {
                if (c == ',' || c == '\n' || c == '\r') {
                    // 필드 끝
                    processFieldOptimized(colIndex, tokenStart, curr, hasEscapedQuotes, res);
                    
                    if (c == ',') {
                        colIndex++;
                    } else {
                        // 줄바꿈: 행 끝
                        // 부족한 컬럼 채우기
                        while (++colIndex < headers.size()) {
                            processFieldOptimized(colIndex, nullptr, nullptr, false, res);
                        }
                        colIndex = 0;
                        res.rowCount++;
                        
                        // CRLF 처리
                        if (c == '\r' && curr + 1 < chunkEnd && *(curr + 1) == '\n') {
                            curr++;
                        }
                    }
                    
                    tokenStart = curr + 1;
                    hasEscapedQuotes = false;
                }
            }
            curr++;
        }
        
        // 마지막 필드 처리
        if (tokenStart < chunkEnd || colIndex > 0) {
            processFieldOptimized(colIndex, tokenStart, chunkEnd, hasEscapedQuotes, res);
            res.rowCount++;
        }
    }

    // 샘플링용: 한 줄 파싱 (기존 로직 유지)
    bool parseLine(vector<string>& tokens) {
        if (cursor >= end) return false;
        bool inQuotes = false;
        string token;
        token.reserve(64);
        while (cursor < end) {
            char c = *cursor;
            if (c == '\n' || c == '\r') {
                if (!inQuotes) {
                    tokens.push_back(token);
                    if (c == '\r' && (cursor + 1 < end) && *(cursor + 1) == '\n') cursor++;
                    cursor++;
                    return true;
                }
            }
            if (c == '"') {
                if (inQuotes && (cursor + 1 < end) && *(cursor + 1) == '"') {
                    token += '"'; cursor++;
                } else inQuotes = !inQuotes;
            } else if (c == ',' && !inQuotes) {
                tokens.push_back(token); token.clear();
            } else token += c;
            cursor++;
        }
        if (!token.empty() || !tokens.empty()) {
            tokens.push_back(token);
            return true;
        }
        return false;
    }

    // 샘플링용: 행 처리
    void processRow(const vector<string>& row) {
        rowCount++;
        for (int c = 0; c < headers.size(); c++) {
            const string& val = row[c];
            bool isNumeric = (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT);
            if (TypeChecker::isNull(val)) {
                stats[c].nullCount++;
                if (isNumeric) {
                    numericData[c].push_back(nan(""));
                } else {
                    stringData[c].push_back("");
                }
                continue;
            }
            if (isNumeric) {
                string cleaned = cleanNumericString(val);
                char* endPtr;
                double num = strtod(cleaned.c_str(), &endPtr);
                if (endPtr == cleaned.c_str()) num = nan("");
                numericData[c].push_back(num);
                if (!isnan(num)) addNumericValue(stats[c], num);
            } else {
                stringData[c].push_back(val);
                stats[c].minLength = min(stats[c].minLength, (uint32_t)val.length());
                stats[c].maxLength = max(stats[c].maxLength, (uint32_t)val.length());
            }
        }
    }

    // 최적화된 필드 처리 (포인터 기반)
    void processFieldOptimized(int colIndex, const char* start, const char* endPtr, bool hasEscapedQuotes, ThreadResult& res) {
        if (colIndex >= headers.size()) return;

        // 빈 필드 처리
        if (start == nullptr || start >= endPtr) {
            res.stats[colIndex].nullCount++;
            if (columnTypes[colIndex] == DataType::INTEGER || columnTypes[colIndex] == DataType::FLOAT) {
                res.numericData[colIndex].push_back(nan(""));
            } else {
                res.stringData[colIndex].push_back("");
            }
            return;
        }

        DataType type = res.stats[colIndex].type;
        bool isNumeric = (type == DataType::INTEGER || type == DataType::FLOAT);

        if (isNumeric) {
            // 숫자형: 문자열 생성 없이 바로 파싱 (가장 큰 성능 향상 포인트)
            double val;
            if (hasEscapedQuotes) {
                // 이스케이프된 따옴표가 있는 복잡한 경우 (드묾) -> 문자열 생성 후 파싱
                string s;
                s.reserve(endPtr - start);
                bool q = false;
                for (const char* p = start; p < endPtr; p++) {
                    if (*p == '"') {
                        if (q && p + 1 < endPtr && *(p + 1) == '"') { s += '"'; p++; }
                        else q = !q;
                    } else s += *p;
                }
                val = fastParseDouble(s.c_str(), s.c_str() + s.length());
            } else {
                // 일반적인 경우: 원본 버퍼에서 바로 파싱
                val = fastParseDouble(start, endPtr);
            }

            res.numericData[colIndex].push_back(val);
            if (!isnan(val)) addNumericValue(res.stats[colIndex], val);
        } else {
            // 문자열형
            string s;
            s.reserve(endPtr - start);
            bool q = false;
            for (const char* p = start; p < endPtr; p++) {
                if (*p == '"') {
                    if (q && p + 1 < endPtr && *(p + 1) == '"') { s += '"'; p++; }
                    else q = !q;
                } else s += *p;
            }

            if (s.empty() || (s.length() <= 4 && TypeChecker::isNull(s))) {
                res.stats[colIndex].nullCount++;
                res.stringData[colIndex].push_back("");
            } else {
                res.stringData[colIndex].push_back(s);
                res.stats[colIndex].minLength = min(res.stats[colIndex].minLength, (uint32_t)s.length());
                res.stats[colIndex].maxLength = max(res.stats[colIndex].maxLength, (uint32_t)s.length());
            }
        }
    }

    // 스레드 결과 병합
    void mergeThreadResult(const ThreadResult& res) {
        this->rowCount += res.rowCount;
        for (size_t i = 0; i < headers.size(); i++) {
            // 데이터 병합
            if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
                numericData[i].insert(numericData[i].end(), res.numericData[i].begin(), res.numericData[i].end());
            } else {
                stringData[i].insert(stringData[i].end(), res.stringData[i].begin(), res.stringData[i].end());
            }

            // 통계 병합
            ColumnStats& target = stats[i];
            const ColumnStats& source = res.stats[i];

            double n1 = target.count;
            double n2 = source.count;

            target.count += source.count;
            target.nullCount += source.nullCount;
            target.sum += source.sum;

            if (isnan(target.min)) target.min = source.min;
            else if (!isnan(source.min)) target.min = min(target.min, source.min);

            if (isnan(target.max)) target.max = source.max;
            else if (!isnan(source.max)) target.max = max(target.max, source.max);

            // Welford 알고리즘 병합 (평균, 분산)
            if (n2 > 0) {
                if (n1 == 0) {
                    target.mean = source.mean;
                    target.m2 = source.m2;
                } else {
                    double delta = source.mean - target.mean;
                    target.mean += delta * n2 / (n1 + n2);
                    target.m2 += source.m2 + delta * delta * n1 * n2 / (n1 + n2);
                }
            }

            target.minLength = min(target.minLength, source.minLength);
            target.maxLength = max(target.maxLength, source.maxLength);
        }
    }

public:
    // 메타데이터(통계)만 JSON 문자열로 반환
    string getMetadata(const string& filename) {
        ostringstream json;
        json << fixed << setprecision(2);
        json << "{\"filename\":\"" << escapeJson(filename) << "\"";
        json << ",\"totalRows\":" << rowCount;
        json << ",\"totalColumns\":" << headers.size();
        json << ",\"columns\":[";

        for (size_t i = 0; i < headers.size(); i++) {
            if (i > 0) json << ",";
            json << "{\"name\":\"" << escapeJson(headers[i]) << "\"";
            json << ",\"type\":\"" << dataTypeToString(columnTypes[i]) << "\"";
            json << ",\"stats\":{\"count\":" << (rowCount - stats[i].nullCount);
            json << ",\"nullCount\":" << stats[i].nullCount;
            
            if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
                if (stats[i].count > 0) {
                    json << ",\"min\":"; jsonSafeDouble(json, stats[i].min);
                    json << ",\"max\":"; jsonSafeDouble(json, stats[i].max);
                    json << ",\"avg\":"; jsonSafeDouble(json, stats[i].mean);
                }
            }
            json << "}}";
        }
        json << "]}";
        return json.str();
    }

    vector<string> getHeaders() { return headers; }
    vector<string> getStringData(int col) { return stringData[col]; }
    
    // 숫자 데이터의 메모리 주소 반환 (Zero-Copy 핵심)
    size_t getNumericDataPtr(int col) {
        return reinterpret_cast<size_t>(numericData[col].data());
    }

    // 문자열 데이터를 Packed 형태로 준비 (JS에서 한 번에 가져가기 위함)
    void prepareStringColumn(int col) {
        if (col < 0 || col >= stringData.size()) return;
        
        const vector<string>& colData = stringData[col];
        size_t totalLen = 0;
        for (const string& s : colData) {
            totalLen += s.length();
        }
        
        packedData.resize(totalLen);
        packedOffsets.resize(colData.size() + 1);
        
        uint8_t* dataPtr = packedData.data();
        uint32_t* offsetPtr = packedOffsets.data();
        
        uint32_t currentOffset = 0;
        offsetPtr[0] = 0;
        
        for (size_t i = 0; i < colData.size(); i++) {
            const string& s = colData[i];
            if (!s.empty()) {
                memcpy(dataPtr + currentOffset, s.data(), s.length());
                currentOffset += s.length();
            }
            offsetPtr[i + 1] = currentOffset;
        }
    }

    size_t getPackedDataPtr() {
        return reinterpret_cast<size_t>(packedData.data());
    }

    size_t getPackedOffsetsPtr() {
        return reinterpret_cast<size_t>(packedOffsets.data());
    }

    // C++에서 JSON 문자열 전체를 조립하여 반환 (JS 오버헤드 최소화)
    string getJson(const string& filename) {
        ostringstream json;
        json << fixed << setprecision(16);

        json << "{\"metadata\":{\"filename\":\"" << escapeJson(filename) << "\"";
        json << ",\"totalRows\":" << rowCount;
        json << ",\"totalColumns\":" << headers.size();
        
        json << ",\"columns\":[";
        for (size_t i = 0; i < headers.size(); i++) {
            if (i > 0) json << ",";
            json << "{\"name\":\"" << escapeJson(headers[i]) << "\",\"type\":\"" << dataTypeToString(columnTypes[i]) << "\"}";
        }
        json << "]";
        
        json << "},\"data\":[";

        vector<string> escapedHeaders;
        escapedHeaders.reserve(headers.size());
        for(const auto& h : headers) escapedHeaders.push_back(escapeJson(h));

        for (int r = 0; r < rowCount; r++) {
            if (r > 0) json << ",";
            json << "{";
            for (size_t c = 0; c < headers.size(); c++) {
                if (c > 0) json << ",";
                json << "\"" << escapedHeaders[c] << "\":";
                
                if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                    jsonSafeDouble(json, numericData[c][r]);
                } else {
                    json << "\"" << escapeJson(stringData[c][r]) << "\"";
                }
            }
            json << "}";
        }
        json << "]}";
        return json.str();
    }
};

EMSCRIPTEN_BINDINGS(my_module) {
    emscripten::function("convertToJsonOptimized", &convertToJsonOptimized);
    
    register_vector<string>("VectorString");
    
    class_<WasmCSVParser>("WasmCSVParser")
        .constructor<>()
        .function("parse", &WasmCSVParser::parse)
        .function("parseBytes", &WasmCSVParser::parseBytes)
        .function("getMetadata", &WasmCSVParser::getMetadata)
        .function("getHeaders", &WasmCSVParser::getHeaders)
        .function("getStringData", &WasmCSVParser::getStringData)
        .function("getNumericDataPtr", &WasmCSVParser::getNumericDataPtr)
        .function("prepareStringColumn", &WasmCSVParser::prepareStringColumn)
        .function("getPackedDataPtr", &WasmCSVParser::getPackedDataPtr)
        .function("getPackedOffsetsPtr", &WasmCSVParser::getPackedOffsetsPtr)
        .function("getJson", &WasmCSVParser::getJson);
}