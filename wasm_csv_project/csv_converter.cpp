#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <iomanip>
#include <ctime>
#include <memory>

using namespace emscripten;

// ============================================================================
// CSV to JSON Converter - Unified Version
// Combines basic parsing improvements with optimized large file processing
// ============================================================================

// =========================
// Utility Functions
// =========================

inline std::string trim(const std::string& str) {
    const char* whitespace = " \t\r\n";
    size_t first = str.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, last - first + 1);
}

// Remove BOM from string
std::string removeBOM(const std::string& str) {
    if (str.length() >= 3 &&
        (unsigned char)str[0] == 0xEF &&
        (unsigned char)str[1] == 0xBB &&
        (unsigned char)str[2] == 0xBF) {
        return str.substr(3);
    }
    return str;
}

// Normalize line endings to \n
std::string normalizeLineEndings(const std::string& str) {
    std::string result;
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

inline std::string escapeJson(const std::string& str) {
    std::string result;
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

inline std::string dataTypeToString(DataType type) {
    switch (type) {
        case DataType::INTEGER: return "integer";
        case DataType::FLOAT: return "float";
        case DataType::BOOLEAN: return "boolean";
        case DataType::DATE: return "date";
        case DataType::STRING: return "string";
    }
    return "string";
}

// Fast type checking
class TypeChecker {
private:
    static constexpr bool isDigitTable[256] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

public:
    static inline bool isDigit(char c) {
        return isDigitTable[static_cast<unsigned char>(c)];
    }

    static bool isInteger(const std::string& str) {
        if (str.empty()) return false;
        size_t start = (str[0] == '-' || str[0] == '+') ? 1 : 0;
        if (start >= str.length()) return false;
        for (size_t i = start; i < str.length(); i++) {
            if (!isDigit(str[i])) return false;
        }
        return true;
    }

    static bool isFloat(const std::string& str) {
        if (str.empty()) return false;
        bool hasDecimal = false;
        bool hasExponent = false;
        size_t start = (str[0] == '-' || str[0] == '+') ? 1 : 0;
        if (start >= str.length()) return false;

        for (size_t i = start; i < str.length(); i++) {
            char c = str[i];
            if (c == '.') {
                if (hasDecimal || hasExponent) return false;
                hasDecimal = true;
            } else if (c == 'e' || c == 'E') {
                if (hasExponent || i == start) return false;
                hasExponent = true;
                if (i + 1 < str.length() && (str[i + 1] == '+' || str[i + 1] == '-')) {
                    i++;
                }
            } else if (!isDigit(c)) {
                return false;
            }
        }
        return hasDecimal || hasExponent;
    }

    static bool isBoolean(const std::string& str) {
        if (str.length() > 5) return false;
        std::string lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower == "true" || lower == "false" || lower == "yes" ||
               lower == "no" || lower == "1" || lower == "0";
    }

    static bool isDate(const std::string& str) {
        if (str.length() < 8 || str.length() > 10) return false;
        if ((str[4] == '-' || str[4] == '/') &&
            (str.length() >= 10 && (str[7] == '-' || str[7] == '/'))) {
            return isDigit(str[0]) && isDigit(str[1]) && isDigit(str[2]) && isDigit(str[3]) &&
                   isDigit(str[5]) && isDigit(str[6]) && isDigit(str[8]) && isDigit(str[9]);
        }
        return false;
    }

    static bool isNull(const std::string& str) {
        if (str.empty()) return true;
        if (str.length() > 4) return false;
        std::string lower = str;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        return lower == "null" || lower == "na" || lower == "n/a" ||
               lower == "nan" || lower == "-" || lower == "";
    }
};

// =========================
// CSV Parser
// =========================

struct CSVParseResult {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    char delimiter;
};

char detectDelimiter(const std::string& content) {
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

// Full CSV parser that handles quoted fields with newlines
CSVParseResult parseCSV(const std::string& content) {
    CSVParseResult result;
    result.delimiter = detectDelimiter(content);

    std::vector<std::string> currentRow;
    std::string field;
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
        } else if (c == '\n' && !inQuotes) {
            currentRow.push_back(trim(field));
            field.clear();

            bool isEmpty = true;
            for (const auto& f : currentRow) {
                if (!f.empty()) {
                    isEmpty = false;
                    break;
                }
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
            if (!f.empty()) {
                isEmpty = false;
                break;
            }
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

// =========================
// Column Statistics
// =========================

struct ColumnStats {
    DataType type = DataType::STRING;
    uint32_t nullCount = 0;
    uint32_t uniqueCount = 0;
    double min = 0;
    double max = 0;
    double sum = 0;
    double mean = 0;
    uint32_t count = 0;
    uint32_t minLength = UINT32_MAX;
    uint32_t maxLength = 0;
    uint32_t trueCount = 0;
    uint32_t falseCount = 0;
    std::string minDate;
    std::string maxDate;
    double m2 = 0; // For Welford's algorithm

    void addNumericValue(double value) {
        count++;
        sum += value;

        if (count == 1) {
            min = max = value;
            mean = value;
        } else {
            min = std::min(min, value);
            max = std::max(max, value);

            double delta = value - mean;
            mean += delta / count;
            double delta2 = value - mean;
            m2 += delta * delta2;
        }
    }

    double getStdDev() const {
        return count > 1 ? std::sqrt(m2 / (count - 1)) : 0;
    }
};

DataType detectColumnType(const std::vector<std::string>& values) {
    bool allInteger = true;
    bool allFloat = true;
    bool allBoolean = true;
    bool allDate = true;
    int nonNullCount = 0;

    for (const auto& val : values) {
        if (TypeChecker::isNull(val)) continue;
        nonNullCount++;

        if (allInteger && !TypeChecker::isInteger(val)) allInteger = false;
        if (allFloat && !TypeChecker::isFloat(val) && !TypeChecker::isInteger(val)) allFloat = false;
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

// =========================
// Main Conversion Functions
// =========================

std::string convertToJson(const std::string& csvContent, const std::string& filename) {
    // Preprocess
    std::string content = removeBOM(csvContent);
    content = normalizeLineEndings(content);

    // Parse CSV
    CSVParseResult parsed = parseCSV(content);

    std::vector<std::string>& headers = parsed.headers;
    std::vector<std::vector<std::string>>& rows = parsed.rows;
    char delimiter = parsed.delimiter;

    if (headers.empty()) {
        return "{\"error\": \"Empty or invalid CSV file\", \"metadata\": {\"filename\": \"" + escapeJson(filename) + "\"}}";
    }

    int numColumns = headers.size();
    int numRows = rows.size();

    // Normalize row lengths
    for (auto& row : rows) {
        while ((int)row.size() < numColumns) {
            row.push_back("");
        }
        if ((int)row.size() > numColumns) {
            row.resize(numColumns);
        }
    }

    // Collect column values
    std::vector<std::vector<std::string>> columnValues(numColumns);
    for (const auto& row : rows) {
        for (int i = 0; i < numColumns && i < (int)row.size(); i++) {
            columnValues[i].push_back(row[i]);
        }
    }

    // Detect types and calculate statistics
    std::vector<ColumnStats> stats(numColumns);
    std::vector<DataType> columnTypes(numColumns);

    for (int i = 0; i < numColumns; i++) {
        columnTypes[i] = detectColumnType(columnValues[i]);
        stats[i].type = columnTypes[i];
        std::set<std::string> uniqueValues;

        for (const auto& val : columnValues[i]) {
            if (TypeChecker::isNull(val)) {
                stats[i].nullCount++;
                continue;
            }

            uniqueValues.insert(val);

            if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
                try {
                    double num = std::stod(val);
                    stats[i].addNumericValue(num);
                } catch (...) {}
            } else if (columnTypes[i] == DataType::STRING) {
                uint32_t len = val.length();
                stats[i].minLength = std::min(stats[i].minLength, len);
                stats[i].maxLength = std::max(stats[i].maxLength, len);
            } else if (columnTypes[i] == DataType::BOOLEAN) {
                std::string lower = val;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "true" || lower == "yes" || lower == "1") {
                    stats[i].trueCount++;
                } else {
                    stats[i].falseCount++;
                }
            } else if (columnTypes[i] == DataType::DATE) {
                if (stats[i].minDate.empty() || val < stats[i].minDate) {
                    stats[i].minDate = val;
                }
                if (stats[i].maxDate.empty() || val > stats[i].maxDate) {
                    stats[i].maxDate = val;
                }
            }
        }

        stats[i].uniqueCount = uniqueValues.size();
        if (stats[i].minLength == UINT32_MAX) stats[i].minLength = 0;
    }

    // Build JSON output
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);

    std::time_t now = std::time(nullptr);
    char timestamp[30];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

    json << "{\n";

    // Metadata
    json << "  \"metadata\": {\n";
    json << "    \"filename\": \"" << escapeJson(filename) << "\",\n";
    json << "    \"total_rows\": " << numRows << ",\n";
    json << "    \"total_columns\": " << numColumns << ",\n";
    json << "    \"file_size_bytes\": " << csvContent.size() << ",\n";
    json << "    \"encoding\": \"UTF-8\",\n";
    json << "    \"delimiter\": \"" << (delimiter == '\t' ? "\\t" : std::string(1, delimiter)) << "\",\n";
    json << "    \"has_header\": true,\n";
    json << "    \"created_at\": \"" << timestamp << "\"\n";
    json << "  },\n";

    // Schema
    json << "  \"schema\": {\n";
    json << "    \"columns\": [\n";
    for (int i = 0; i < numColumns; i++) {
        json << "      {\n";
        json << "        \"index\": " << i << ",\n";
        json << "        \"name\": \"" << escapeJson(headers[i]) << "\",\n";
        json << "        \"type\": \"" << dataTypeToString(columnTypes[i]) << "\",\n";
        json << "        \"nullable\": " << (stats[i].nullCount > 0 ? "true" : "false") << ",\n";
        json << "        \"unique\": " << (stats[i].uniqueCount == (uint32_t)(numRows - stats[i].nullCount) ? "true" : "false") << "\n";
        json << "      }" << (i < numColumns - 1 ? "," : "") << "\n";
    }
    json << "    ]\n";
    json << "  },\n";

    // Statistics
    json << "  \"statistics\": {\n";
    json << "    \"columns\": {\n";
    for (int i = 0; i < numColumns; i++) {
        json << "      \"" << escapeJson(headers[i]) << "\": {\n";
        json << "        \"type\": \"" << dataTypeToString(columnTypes[i]) << "\",\n";
        json << "        \"null_count\": " << stats[i].nullCount << ",\n";
        json << "        \"unique_count\": " << stats[i].uniqueCount;

        if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
            json << ",\n        \"min\": " << stats[i].min;
            json << ",\n        \"max\": " << stats[i].max;
            json << ",\n        \"mean\": " << stats[i].mean;
            json << ",\n        \"std_dev\": " << stats[i].getStdDev();
            json << ",\n        \"sum\": " << stats[i].sum;
        } else if (columnTypes[i] == DataType::STRING) {
            json << ",\n        \"min_length\": " << stats[i].minLength;
            json << ",\n        \"max_length\": " << stats[i].maxLength;
        } else if (columnTypes[i] == DataType::BOOLEAN) {
            json << ",\n        \"true_count\": " << stats[i].trueCount;
            json << ",\n        \"false_count\": " << stats[i].falseCount;
        } else if (columnTypes[i] == DataType::DATE) {
            json << ",\n        \"min\": \"" << stats[i].minDate << "\"";
            json << ",\n        \"max\": \"" << stats[i].maxDate << "\"";
        }

        json << "\n      }" << (i < numColumns - 1 ? "," : "") << "\n";
    }
    json << "    }\n";
    json << "  },\n";

    // Data
    json << "  \"data\": [\n";
    for (int r = 0; r < numRows; r++) {
        json << "    {";
        for (int c = 0; c < numColumns; c++) {
            std::string value = (c < (int)rows[r].size()) ? rows[r][c] : "";
            json << "\n      \"" << escapeJson(headers[c]) << "\": ";

            if (TypeChecker::isNull(value)) {
                json << "null";
            } else if (columnTypes[c] == DataType::INTEGER) {
                json << value;
            } else if (columnTypes[c] == DataType::FLOAT) {
                try {
                    json << std::stod(value);
                } catch (...) {
                    json << "\"" << escapeJson(value) << "\"";
                }
            } else if (columnTypes[c] == DataType::BOOLEAN) {
                std::string lower = value;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                json << ((lower == "true" || lower == "yes" || lower == "1") ? "true" : "false");
            } else {
                json << "\"" << escapeJson(value) << "\"";
            }

            if (c < numColumns - 1) json << ",";
        }
        json << "\n    }" << (r < numRows - 1 ? "," : "") << "\n";
    }
    json << "  ],\n";

    json << "  \"errors\": []\n";
    json << "}";

    return json.str();
}

// Metadata-only version for very large files
std::string convertToJsonMetadataOnly(const std::string& csvContent, const std::string& filename) {
    std::string content = removeBOM(csvContent);
    content = normalizeLineEndings(content);

    CSVParseResult parsed = parseCSV(content);

    std::vector<std::string>& headers = parsed.headers;
    std::vector<std::vector<std::string>>& rows = parsed.rows;
    char delimiter = parsed.delimiter;

    if (headers.empty()) {
        return "{\"error\": \"Empty or invalid CSV file\", \"metadata\": {\"filename\": \"" + escapeJson(filename) + "\"}}";
    }

    int numColumns = headers.size();
    int numRows = rows.size();

    // Normalize row lengths
    for (auto& row : rows) {
        while ((int)row.size() < numColumns) {
            row.push_back("");
        }
        if ((int)row.size() > numColumns) {
            row.resize(numColumns);
        }
    }

    // Collect samples for type detection (first 1000 rows)
    std::vector<std::vector<std::string>> sampleData(numColumns);
    int sampleSize = std::min(numRows, 1000);

    for (int r = 0; r < sampleSize; r++) {
        for (int i = 0; i < numColumns && i < (int)rows[r].size(); i++) {
            sampleData[i].push_back(rows[r][i]);
        }
    }

    // Detect types and calculate statistics
    std::vector<ColumnStats> stats(numColumns);
    std::vector<DataType> columnTypes(numColumns);
    std::vector<std::unordered_set<std::string>> uniqueValues(numColumns);

    for (int i = 0; i < numColumns; i++) {
        columnTypes[i] = detectColumnType(sampleData[i]);
        stats[i].type = columnTypes[i];
    }

    // Process all rows for statistics
    for (const auto& row : rows) {
        for (int i = 0; i < numColumns && i < (int)row.size(); i++) {
            const std::string& val = row[i];

            if (TypeChecker::isNull(val)) {
                stats[i].nullCount++;
                continue;
            }

            if (uniqueValues[i].size() < 10000) {
                uniqueValues[i].insert(val);
            }

            if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
                try {
                    double num = std::stod(val);
                    stats[i].addNumericValue(num);
                } catch (...) {}
            }
        }
    }

    for (int i = 0; i < numColumns; i++) {
        stats[i].uniqueCount = uniqueValues[i].size();
    }

    // Build JSON output (metadata only)
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);

    std::time_t now = std::time(nullptr);
    char timestamp[30];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

    json << "{\n";

    // Metadata
    json << "  \"metadata\": {\n";
    json << "    \"filename\": \"" << escapeJson(filename) << "\",\n";
    json << "    \"total_rows\": " << numRows << ",\n";
    json << "    \"total_columns\": " << numColumns << ",\n";
    json << "    \"file_size_bytes\": " << csvContent.size() << ",\n";
    json << "    \"encoding\": \"UTF-8\",\n";
    json << "    \"delimiter\": \"" << (delimiter == '\t' ? "\\t" : std::string(1, delimiter)) << "\",\n";
    json << "    \"has_header\": true,\n";
    json << "    \"created_at\": \"" << timestamp << "\",\n";
    json << "    \"data_included\": false,\n";
    json << "    \"processing_info\": {\n";
    json << "      \"reason\": \"Large file - data excluded to save memory\"\n";
    json << "    }\n";
    json << "  },\n";

    // Schema
    json << "  \"schema\": {\n";
    json << "    \"columns\": [\n";
    for (int i = 0; i < numColumns; i++) {
        json << "      {\n";
        json << "        \"index\": " << i << ",\n";
        json << "        \"name\": \"" << escapeJson(headers[i]) << "\",\n";
        json << "        \"type\": \"" << dataTypeToString(columnTypes[i]) << "\",\n";
        json << "        \"nullable\": " << (stats[i].nullCount > 0 ? "true" : "false") << "\n";
        json << "      }" << (i < numColumns - 1 ? "," : "") << "\n";
    }
    json << "    ]\n";
    json << "  },\n";

    // Statistics
    json << "  \"statistics\": {\n";
    json << "    \"columns\": {\n";
    for (int i = 0; i < numColumns; i++) {
        json << "      \"" << escapeJson(headers[i]) << "\": {\n";
        json << "        \"type\": \"" << dataTypeToString(columnTypes[i]) << "\",\n";
        json << "        \"null_count\": " << stats[i].nullCount << ",\n";
        json << "        \"unique_count\": " << stats[i].uniqueCount;

        if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
            if (stats[i].count > 0) {
                json << ",\n        \"min\": " << stats[i].min;
                json << ",\n        \"max\": " << stats[i].max;
                json << ",\n        \"mean\": " << stats[i].mean;
                json << ",\n        \"std_dev\": " << stats[i].getStdDev();
            }
        }

        json << "\n      }" << (i < numColumns - 1 ? "," : "") << "\n";
    }
    json << "    }\n";
    json << "  },\n";

    json << "  \"data\": [],\n";
    json << "  \"errors\": []\n";
    json << "}";

    return json.str();
}

// Auto-select based on file size
std::string convertToJsonAuto(const std::string& csvContent, const std::string& filename) {
    // If file is larger than 10MB, use metadata-only mode
    if (csvContent.size() > 10 * 1024 * 1024) {
        return convertToJsonMetadataOnly(csvContent, filename);
    }
    return convertToJson(csvContent, filename);
}

// =========================
// Optimized Version
// =========================

// Fast in-place trim
inline void trimInPlace(std::string& str) {
    const char* whitespace = " \t\r\n";
    size_t first = str.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        str.clear();
        return;
    }
    size_t last = str.find_last_not_of(whitespace);
    if (first > 0 || last < str.length() - 1) {
        str = str.substr(first, last - first + 1);
    }
}

// Fast BOM removal (returns pointer offset)
inline const char* skipBOM(const char* data, size_t& length) {
    if (length >= 3 &&
        (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        length -= 3;
        return data + 3;
    }
    return data;
}

// Optimized version with aggressive performance optimizations
std::string convertToJsonOptimized(const std::string& csvContent, const std::string& filename) {
    // Skip BOM
    size_t dataLength = csvContent.length();
    const char* data = skipBOM(csvContent.c_str(), dataLength);

    // Detect delimiter
    char delimiter = ',';
    {
        uint32_t commaCount = 0, tabCount = 0, semicolonCount = 0;
        bool inQuotes = false;
        uint32_t lineCount = 0;

        for (size_t i = 0; i < dataLength && lineCount < 5; i++) {
            char c = data[i];
            if (c == '"') inQuotes = !inQuotes;
            else if (!inQuotes) {
                if (c == ',') commaCount++;
                else if (c == '\t') tabCount++;
                else if (c == ';') semicolonCount++;
                else if (c == '\n') lineCount++;
            }
        }

        if (tabCount > commaCount && tabCount > semicolonCount) delimiter = '\t';
        else if (semicolonCount > commaCount && semicolonCount > tabCount) delimiter = ';';
    }

    // Parse CSV with pre-allocated memory
    std::vector<std::string> headers;
    headers.reserve(50);

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> currentRow;
    currentRow.reserve(50);

    std::string field;
    field.reserve(256);

    bool inQuotes = false;
    bool isFirstRow = true;

    for (size_t i = 0; i < dataLength; i++) {
        char c = data[i];

        if (c == '"') {
            if (inQuotes && i + 1 < dataLength && data[i + 1] == '"') {
                field += '"';
                i++;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == delimiter && !inQuotes) {
            trimInPlace(field);
            currentRow.push_back(std::move(field));
            field.clear();
            field.reserve(256);
        } else if ((c == '\n' || c == '\r') && !inQuotes) {
            if (c == '\r' && i + 1 < dataLength && data[i + 1] == '\n') i++;

            trimInPlace(field);
            currentRow.push_back(std::move(field));
            field.clear();
            field.reserve(256);

            bool hasData = false;
            for (const auto& f : currentRow) {
                if (!f.empty()) {
                    hasData = true;
                    break;
                }
            }

            if (hasData) {
                if (isFirstRow) {
                    headers = std::move(currentRow);
                    isFirstRow = false;
                    size_t estimatedRows = dataLength / (headers.size() * 20);
                    rows.reserve(std::min(estimatedRows, size_t(100000)));
                } else {
                    rows.push_back(std::move(currentRow));
                }
            }
            currentRow.clear();
            currentRow.reserve(headers.size());
        } else {
            field += c;
        }
    }

    // Last row
    if (!field.empty() || !currentRow.empty()) {
        trimInPlace(field);
        currentRow.push_back(std::move(field));
        bool hasData = false;
        for (const auto& f : currentRow) {
            if (!f.empty()) { hasData = true; break; }
        }
        if (hasData) {
            if (isFirstRow) headers = std::move(currentRow);
            else rows.push_back(std::move(currentRow));
        }
    }

    if (headers.empty()) {
        return "{\"error\":\"Empty CSV\",\"metadata\":{\"filename\":\"" + filename + "\"}}";
    }

    const int numColumns = headers.size();
    const int numRows = rows.size();

    // Normalize row lengths
    for (auto& row : rows) {
        row.resize(numColumns);
    }

    // Transpose for column-wise processing
    std::vector<std::vector<std::string>> columnData(numColumns);
    for (int i = 0; i < numColumns; i++) {
        columnData[i].reserve(numRows);
    }
    for (const auto& row : rows) {
        for (int i = 0; i < numColumns; i++) {
            columnData[i].push_back(row[i]);
        }
    }

    // Type detection and stats (simplified)
    std::vector<DataType> columnTypes(numColumns);
    std::vector<ColumnStats> stats(numColumns);

    for (int i = 0; i < numColumns; i++) {
        columnTypes[i] = detectColumnType(columnData[i]);
        stats[i].type = columnTypes[i];

        std::unordered_set<std::string> uniqueVals;
        uniqueVals.reserve(std::min(numRows, 10000));

        for (const auto& val : columnData[i]) {
            if (TypeChecker::isNull(val)) {
                stats[i].nullCount++;
                continue;
            }
            if (uniqueVals.size() < 10000) uniqueVals.insert(val);

            if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
                try {
                    double num = std::stod(val);
                    stats[i].addNumericValue(num);
                } catch (...) {}
            }
        }
        stats[i].uniqueCount = uniqueVals.size();
    }

    // Build JSON
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);

    json << "{\"metadata\":{\"filename\":\"" << escapeJson(filename) << "\"";
    json << ",\"totalRows\":" << numRows;
    json << ",\"totalColumns\":" << numColumns;
    json << ",\"fileSizeBytes\":" << csvContent.length();
    json << ",\"columns\":[";

    for (int i = 0; i < numColumns; i++) {
        if (i > 0) json << ",";
        json << "{\"name\":\"" << escapeJson(headers[i]) << "\"";
        json << ",\"type\":\"" << dataTypeToString(columnTypes[i]) << "\"";
        json << ",\"stats\":{\"count\":" << (numRows - stats[i].nullCount);
        json << ",\"unique\":" << stats[i].uniqueCount;
        json << ",\"nullCount\":" << stats[i].nullCount;

        if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
            json << ",\"min\":" << stats[i].min;
            json << ",\"max\":" << stats[i].max;
            json << ",\"avg\":" << (stats[i].count > 0 ? stats[i].sum / stats[i].count : 0);
        }
        json << "}}";
    }

    json << "]},\"data\":[";

    for (int r = 0; r < numRows; r++) {
        if (r > 0) json << ",";
        json << "{";
        for (int c = 0; c < numColumns; c++) {
            if (c > 0) json << ",";
            json << "\"" << escapeJson(headers[c]) << "\":";

            const std::string& val = rows[r][c];
            if (val.empty()) {
                json << "null";
            } else if (columnTypes[c] == DataType::INTEGER || columnTypes[c] == DataType::FLOAT) {
                try {
                    std::stod(val);
                    json << val;
                } catch (...) {
                    json << "\"" << escapeJson(val) << "\"";
                }
            } else if (columnTypes[c] == DataType::BOOLEAN) {
                std::string lower = val;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
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

EMSCRIPTEN_BINDINGS(csv_converter) {
    function("convertToJson", &convertToJson);
    function("convertToJsonMetadataOnly", &convertToJsonMetadataOnly);
    function("convertToJsonAuto", &convertToJsonAuto);
    function("convertToJsonOptimized", &convertToJsonOptimized);
}
