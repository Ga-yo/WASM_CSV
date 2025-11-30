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
    result.reserve(str.size() * 1.1);
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
string cleanNumericString(const string& input) {
    // 1단계: 문자열에서 숫자처럼 보이는 첫 부분을 찾습니다. (쉼표, 소수점, 부호 포함)
    // 예: "₩ 1,234.56 kg" -> "1,234.56"
    static const regex num_regex(R"([+-]?\s*[\d,]+(?:\.\d+)?)");
    smatch match;
    string extracted_num;

    if (regex_search(input, match, num_regex) && !match.empty()) {
        extracted_num = match[0].str();
        
        // 2단계: 찾은 부분에서 쉼표(,)와 공백을 제거합니다.
        extracted_num.erase(remove(extracted_num.begin(), extracted_num.end(), ','), extracted_num.end());
        extracted_num.erase(remove(extracted_num.begin(), extracted_num.end(), ' '), extracted_num.end());

        if (!extracted_num.empty() && !isnan(stringToDouble(extracted_num))) {
            return extracted_num;
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

    // 3. Normalize row lengths and clean numeric strings
    for (auto& row : rows) {
        row.resize(numColumns);
        for (int i = 0; i < numColumns; ++i) {
            // Apply cleaning *before* analysis
            row[i] = cleanNumericString(row[i]); 
        }
    }
    
    // 4. Transpose for column-wise analysis
    vector<vector<string>> columnData(numColumns);
    for (int i = 0; i < numColumns; i++) {
        columnData[i].reserve(numRows);
    }
    for (const auto& row : rows) {
        for (int i = 0; i < numColumns; i++) {
            columnData[i].push_back(row[i]);
        }
    }

    // 5. Determine Types and calculate full stats (using cleaned and transposed data)
    vector<DataType> columnTypes(numColumns);
    vector<ColumnStats> stats(numColumns);
    
    for (int i = 0; i < numColumns; i++) {
        vector<string> sample_data;
        int sampleSize = ::min(numRows, 1000);
        for(int r = 0; r < sampleSize; ++r) {
            sample_data.push_back(columnData[i][r]);
        }
        
        columnTypes[i] = detectColumnType(sample_data);
        stats[i].type = columnTypes[i];
        
        unordered_set<string> uniqueVals;
        uniqueVals.reserve(::min(numRows, 50000)); 

        for (const auto& val : columnData[i]) {
            if (TypeChecker::isNull(val)) {
                stats[i].nullCount++;
                continue;
            }

            if (uniqueVals.size() < 50000) { 
                uniqueVals.insert(val);
            }
            
            if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
                double num = stringToDouble(val);
                if (!isnan(num)) {
                    stats[i].addNumericValue(num);
                }
            } else if (columnTypes[i] == DataType::STRING) {
                uint32_t len = val.length();
                stats[i].minLength = ::min(stats[i].minLength, len);
                stats[i].maxLength = ::max(stats[i].maxLength, len);
            }
        }
        stats[i].uniqueCount = uniqueVals.size();
        if (stats[i].minLength == UINT32_MAX) stats[i].minLength = 0;
    }


    // 6. Build JSON
    ostringstream json;
    json << fixed << setprecision(2);
    
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

    // Data output
    for (int r = 0; r < numRows; r++) {
        if (r > 0) json << ",";
        json << "{";
        for (int c = 0; c < numColumns; c++) {
            if (c > 0) json << ",";
            json << "\"" << escapeJson(headers[c]) << "\":";

            const string& val = rows[r][c];
            if (TypeChecker::isNull(val)) {
                json << "null";
            } else if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                double num = stringToDouble(val);
                jsonSafeDouble(json, num); 
            } else if (columnTypes[c] == DataType::BOOLEAN) {
                string lower = val;
                transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "true" || lower == "yes" || lower == "1") json << "true";
                else if (lower == "false" || lower == "no" || lower == "0") json << "false";
                else json << "\"" << escapeJson(val) << "\"";
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