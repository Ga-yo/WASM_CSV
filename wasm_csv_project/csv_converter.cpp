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

using namespace std;
using namespace emscripten;

// ============================================================================
// CSV to JSON Converter - Single Header Model (Optimized & Stable)
// ============================================================================

// Forward Declarations 
// 모든 핵심 기능은 convertToJsonOptimized에 통합됩니다.
string convertToJsonOptimized(const string& csvContent, const string& filename);
string convertToJson(const string& csvContent, const string& filename);
string convertToJsonMetadataOnly(const string& csvContent, const string& filename);
string convertToJsonAuto(const string& csvContent, const string& filename);

// =========================
// Utility Functions
// =========================

inline string trim(const string& str) {
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

inline string escapeJson(const string& str) {
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

// 💡 High-Performance Numeric Parsing
inline double stringToDouble(const string& str) {
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

// 💡 Numeric String Cleaning (Removes commas and units)
// 정규식을 사용하여 숫자 형식의 문자열을 정리합니다. (예: "₩ 1,234.56 kg" -> "1234.56")
// 최적화: 숫자가 없는 경우 빠르게 반환
string cleanNumericString(const string& input) {
    if (input.empty()) return input;

    // 빠른 경로 1: 이미 순수한 숫자인 경우 (가장 흔한 케이스)
    char first = input[0];
    if (isdigit(first) || first == '-' || first == '+' || first == '.') {
        // 쉼표나 공백이 있는지 빠르게 체크
        bool needs_cleaning = false;
        for (char c : input) {
            if (c == ',' || c == ' ') {
                needs_cleaning = true;
                break;
            }
            // 숫자가 아닌 특수문자 발견 시 정규식 사용 필요
            if (!isdigit(c) && c != '.' && c != '-' && c != '+' && c != 'e' && c != 'E') {
                needs_cleaning = true;
                break;
            }
        }

        // 쉼표나 공백만 있으면 간단히 제거
        if (needs_cleaning) {
            string result;
            result.reserve(input.size());
            for (char c : input) {
                if (c != ',' && c != ' ') {
                    result += c;
                }
            }
            // 정리 후 유효한 숫자인지 확인
            if (!result.empty() && !isnan(stringToDouble(result))) {
                return result;
            }
        } else {
            // 이미 깨끗한 숫자
            return input;
        }
    }

    // 빠른 경로 2: 숫자가 전혀 없으면 원본 반환
    bool has_digit = false;
    for (char c : input) {
        if (isdigit(c)) {
            has_digit = true;
            break;
        }
    }
    if (!has_digit) return input;

    // 느린 경로: 정규식 사용 (통화 기호 등이 포함된 경우)
    static const regex num_regex(R"([+-]?\s*[\d,]+(?:\.\d+)?)");
    smatch match;

    if (regex_search(input, match, num_regex) && !match.empty()) {
        string extracted_num = match[0].str();

        // 쉼표와 공백 제거
        string result;
        result.reserve(extracted_num.size());
        for (char c : extracted_num) {
            if (c != ',' && c != ' ') {
                result += c;
            }
        }

        if (!result.empty() && !isnan(stringToDouble(result))) {
            return result;
        }
    }

    return input; // 숫자 부분을 찾지 못하면 원본 반환
}

// ---------------------------------------------------------------------------------

// =========================
// Type Detection
// =========================

enum class DataType {
    INTEGER,
    FLOAT,
    BOOLEAN,
    DATE,
    STRING
};

inline string dataTypeToString(DataType type) {
    switch (type) {
        case DataType::INTEGER: return "integer";
        case DataType::FLOAT: return "float";
        case DataType::BOOLEAN: return "boolean";
        case DataType::DATE: return "date";
        case DataType::STRING: return "string";
    }
    return "string";
}

class TypeChecker {
public:
    static bool isInteger(const string& str) {
        double num = stringToDouble(str);
        return !isnan(num) && trunc(num) == num;
    }

    static bool isFloat(const string& str) {
        double num = stringToDouble(str);
        return !isnan(num) && trunc(num) != num;
    }

    static bool isNumeric(const string& str) {
        return !isnan(stringToDouble(str));
    }
    
    static bool isBoolean(const string& str) {
        if (str.length() > 5) return false;
        string lower = str;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower == "true" || lower == "false" || lower == "yes" ||
               lower == "no" || lower == "1" || lower == "0";
    }

    static bool isDate(const string& str) {
        if (str.length() < 8 || str.length() > 10) return false;
        // Simplified check for YYYY-MM-DD or YYYY/MM/DD
        if ((str[4] == '-' || str[4] == '/') &&
            (str.length() >= 10 && (str[7] == '-' || str[7] == '/'))) {
            return true; 
        }
        return false;
    }

    static bool isNull(const string& str) {
        if (str.empty()) return true;
        if (str.length() > 4) return false;
        string lower = str;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower == "null" || lower == "na" || lower == "n/a" ||
               lower == "nan" || lower == "-" || lower == "";
    }
};

// ---------------------------------------------------------------------------------

// =========================
// CSV Parser (Single Header Logic Restored)
// =========================

struct CSVParseResult {
    vector<string> headers;
    vector<vector<string>> rows;
    char delimiter;
};

char detectDelimiter(const string& content) {
    size_t commaCount = 0, tabCount = 0, semicolonCount = 0;
    bool inQuotes = false;
    size_t lineCount = 0;

    for (size_t i = 0; i < content.length() && lineCount < 5; i++) {
        char c = content[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (!inQuotes) {
            if (c == ',') commaCount++;
            else if (c == '\t') tabCount++;
            else if (c == ';') semicolonCount++;
            else if (c == '\n') lineCount++;
        }
    }

    if (tabCount > commaCount && tabCount > semicolonCount) return '\t';
    if (semicolonCount > commaCount && semicolonCount > tabCount) return ';';
    return ',';
}

// Restored classic single-header parsing logic
CSVParseResult parseCSV(const string& content) {
    CSVParseResult result;
    result.delimiter = detectDelimiter(content);

    vector<string> currentRow;
    string field;
    bool inQuotes = false;
    bool isFirstRow = true;

    for (size_t i = 0; i < content.length(); i++) {
        char c = content[i];

        if (c == '"') {
            if (inQuotes && i + 1 < content.length() && content[i + 1] == '"') {
                field += '"';
                i++;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == result.delimiter && !inQuotes) {
            currentRow.push_back(trim(field));
            field.clear();
        } else if ((c == '\n' || c == '\r') && !inQuotes) {
            if (c == '\r' && i + 1 < content.length() && content[i + 1] == '\n') i++;

            currentRow.push_back(trim(field));
            field.clear();

            bool isEmpty = true;
            for (const auto& f : currentRow) {
                if (!f.empty()) { isEmpty = false; break; }
            }

            if (!isEmpty) {
                if (isFirstRow) {
                    result.headers = currentRow;
                    isFirstRow = false;
                } else {
                    result.rows.push_back(currentRow);
                }
            }
            currentRow.clear();
        } else {
            field += c;
        }
    }

    // Handle last row
    if (!field.empty() || !currentRow.empty()) {
        currentRow.push_back(trim(field));

        bool isEmpty = true;
        for (const auto& f : currentRow) {
            if (!f.empty()) { isEmpty = false; break; }
        }
        if (!isEmpty) {
            if (isFirstRow) {
                result.headers = currentRow;
            } else {
                result.rows.push_back(currentRow);
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------------

// =========================
// Column Statistics
// =========================

struct ColumnStats {
    DataType type = DataType::STRING;
    uint32_t nullCount = 0;
    uint32_t uniqueCount = 0;
    double min = NAN; 
    double max = NAN; 
    double sum = 0;
    double mean = NAN; 
    uint32_t count = 0;
    uint32_t minLength = UINT32_MAX;
    uint32_t maxLength = 0;
    uint32_t trueCount = 0;
    uint32_t falseCount = 0;
    string minDate;
    string maxDate;
    double m2 = 0; 

    void addNumericValue(double value) {
        count++;
        sum += value;

        if (count == 1) {
            min = max = value;
            mean = value;
        } else {
            min = ::min(min, value);
            max = ::max(max, value);

            double delta = value - mean;
            mean += delta / count;
            double delta2 = value - mean;
            m2 += delta * delta2;
        }
    }

    double getStdDev() const {
        return count > 1 ? sqrt(m2 / (count - 1)) : NAN; 
    }
};

DataType detectColumnType(const vector<string>& values) {
    bool allInteger = true;
    bool allFloat = true;
    bool allBoolean = true;
    bool allDate = true;
    int nonNullCount = 0;

    for (const auto& val : values) {
        if (TypeChecker::isNull(val)) continue;
        nonNullCount++;

        if (!TypeChecker::isNumeric(val)) {
             allInteger = false;
             allFloat = false;
        } else {
            if (allInteger && !TypeChecker::isInteger(val)) allInteger = false;
            if (allFloat && !TypeChecker::isFloat(val) && !TypeChecker::isInteger(val)) allFloat = false;
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
            string val = rows[r][c];

            // 숫자 타입 컬럼만 정리 수행 (대폭 성능 향상)
            if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                val = cleanNumericString(val);
                rows[r][c] = val;  // 정리된 값으로 교체
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
                double num = stringToDouble(val);
                if (!isnan(num)) {
                    stats[c].addNumericValue(num);
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
                json << ",\"std_dev\":"; jsonSafeDouble(json, stats[i].getStdDev());
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
                double num = stringToDouble(val);
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

// =================================================================================
// Emscripten Bindings
// =================================================================================

// 💡 Reverted to 2-argument signatures for bindings
EMSCRIPTEN_BINDINGS(csv_converter) {
    // 'function'이 std::function과 충돌하므로 emscripten::function으로 명시합니다.
    emscripten::function("convertToJson", &convertToJson);
    emscripten::function("convertToJsonMetadataOnly", &convertToJsonMetadataOnly);
    emscripten::function("convertToJsonAuto", &convertToJsonAuto);
    emscripten::function("convertToJsonOptimized", &convertToJsonOptimized);
}