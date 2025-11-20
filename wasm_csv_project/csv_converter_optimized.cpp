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
// WASM의 대용량 파일 처리 이점:
// 1. 네이티브에 가까운 속도 (JavaScript보다 10-100배 빠름)
// 2. 효율적인 메모리 관리 (직접 메모리 제어)
// 3. SIMD 지원으로 병렬 처리 가능
// 4. 멀티스레딩 지원 (Web Workers와 SharedArrayBuffer)
// 5. 스트리밍 처리로 메모리 사용량 최소화
// ============================================================================

// Forward declarations
class ChunkedCSVProcessor;

// Utility functions
inline std::string trim(const std::string& str) {
    const char* whitespace = " \t\r\n";
    size_t first = str.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(whitespace);
    return str.substr(first, last - first + 1);
}

inline std::string escapeJson(const std::string& str) {
    std::string result;
    result.reserve(str.size() * 1.1); // Pre-allocate for efficiency
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

// Type detection
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

// Fast type checking using lookup tables
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
        // Quick check for YYYY-MM-DD or YYYY/MM/DD
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

// Column statistics with memory-efficient storage
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

    // For streaming calculation of variance (Welford's algorithm)
    double m2 = 0;

    void addNumericValue(double value) {
        count++;
        sum += value;

        if (count == 1) {
            min = max = value;
            mean = value;
        } else {
            min = std::min(min, value);
            max = std::max(max, value);

            // Welford's online algorithm for mean and variance
            double delta = value - mean;
            mean += delta / count;
            double delta2 = value - mean;
            m2 += delta * delta2;
        }
    }

    double getVariance() const {
        return count > 1 ? m2 / (count - 1) : 0;
    }

    double getStdDev() const {
        return std::sqrt(getVariance());
    }
};

// Memory-efficient CSV line parser
class FastCSVParser {
private:
    char delimiter;
    std::vector<std::string> fields;
    std::string currentField;

public:
    FastCSVParser(char delim = ',') : delimiter(delim) {
        fields.reserve(50); // Pre-allocate for typical column count
        currentField.reserve(256); // Pre-allocate for typical field size
    }

    const std::vector<std::string>& parseLine(const char* line, size_t length) {
        fields.clear();
        currentField.clear();
        bool inQuotes = false;

        for (size_t i = 0; i < length; i++) {
            char c = line[i];

            if (c == '"') {
                if (inQuotes && i + 1 < length && line[i + 1] == '"') {
                    currentField += '"';
                    i++;
                } else {
                    inQuotes = !inQuotes;
                }
            } else if (c == delimiter && !inQuotes) {
                fields.push_back(trim(currentField));
                currentField.clear();
            } else if (c != '\r') { // Skip carriage return
                currentField += c;
            }
        }
        fields.push_back(trim(currentField));

        return fields;
    }

    void setDelimiter(char delim) {
        delimiter = delim;
    }
};

// Main chunked processor for large files
class ChunkedCSVProcessor {
private:
    static constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
    static constexpr size_t SAMPLE_ROWS = 1000; // Rows to sample for type detection

    std::vector<std::string> headers;
    std::vector<ColumnStats> stats;
    std::vector<DataType> columnTypes;
    FastCSVParser parser;
    char delimiter = ',';
    size_t totalRows = 0;
    size_t totalBytes = 0;

    // For unique value tracking (limited to save memory)
    std::vector<std::unordered_set<std::string>> uniqueValues;
    static constexpr size_t MAX_UNIQUE_TRACK = 10000;

    val progressCallback = val::null();

public:
    void setProgressCallback(val callback) {
        progressCallback = callback;
    }

    char detectDelimiter(const std::string& firstLine) {
        int commaCount = 0, tabCount = 0, semicolonCount = 0;
        bool inQuotes = false;

        for (char c : firstLine) {
            if (c == '"') inQuotes = !inQuotes;
            if (!inQuotes) {
                if (c == ',') commaCount++;
                else if (c == '\t') tabCount++;
                else if (c == ';') semicolonCount++;
            }
        }

        if (tabCount > commaCount && tabCount > semicolonCount) return '\t';
        if (semicolonCount > commaCount && semicolonCount > tabCount) return ';';
        return ',';
    }

    DataType detectColumnType(const std::vector<std::string>& samples) {
        if (samples.empty()) return DataType::STRING;

        bool allInteger = true;
        bool allFloat = true;
        bool allBoolean = true;
        bool allDate = true;
        int nonNullCount = 0;

        for (const auto& val : samples) {
            if (TypeChecker::isNull(val)) continue;
            nonNullCount++;

            if (allInteger && !TypeChecker::isInteger(val)) allInteger = false;
            if (allFloat && !TypeChecker::isFloat(val) && !TypeChecker::isInteger(val)) allFloat = false;
            if (allBoolean && !TypeChecker::isBoolean(val)) allBoolean = false;
            if (allDate && !TypeChecker::isDate(val)) allDate = false;

            // Early exit if only string is possible
            if (!allInteger && !allFloat && !allBoolean && !allDate) break;
        }

        if (nonNullCount == 0) return DataType::STRING;
        if (allBoolean) return DataType::BOOLEAN;
        if (allDate) return DataType::DATE;
        if (allInteger) return DataType::INTEGER;
        if (allFloat) return DataType::FLOAT;
        return DataType::STRING;
    }

    std::string processLargeCSV(const std::string& csvContent, const std::string& filename) {
        totalBytes = csvContent.size();

        // First pass: detect delimiter and parse headers
        size_t firstNewline = csvContent.find('\n');
        if (firstNewline == std::string::npos) {
            return R"({"error": "Invalid CSV: no newline found"})";
        }

        std::string firstLine = csvContent.substr(0, firstNewline);
        delimiter = detectDelimiter(firstLine);
        parser.setDelimiter(delimiter);

        headers = std::vector<std::string>(parser.parseLine(firstLine.c_str(), firstLine.length()));
        size_t numColumns = headers.size();

        // Initialize statistics
        stats.resize(numColumns);
        columnTypes.resize(numColumns, DataType::STRING);
        uniqueValues.resize(numColumns);

        // Sample rows for type detection
        std::vector<std::vector<std::string>> sampleData(numColumns);

        // Process content in chunks
        size_t pos = firstNewline + 1;
        size_t lineStart = pos;
        size_t rowCount = 0;
        size_t processedBytes = 0;

        // First pass: collect samples and count rows
        while (pos < csvContent.size()) {
            if (csvContent[pos] == '\n' || pos == csvContent.size() - 1) {
                size_t lineEnd = (csvContent[pos] == '\n') ? pos : pos + 1;
                size_t lineLength = lineEnd - lineStart;

                if (lineLength > 0) {
                    const auto& fields = parser.parseLine(csvContent.c_str() + lineStart, lineLength);

                    // Collect samples for type detection
                    if (rowCount < SAMPLE_ROWS) {
                        for (size_t i = 0; i < numColumns && i < fields.size(); i++) {
                            sampleData[i].push_back(fields[i]);
                        }
                    }

                    // Update statistics
                    for (size_t i = 0; i < numColumns && i < fields.size(); i++) {
                        const std::string& value = fields[i];

                        if (TypeChecker::isNull(value)) {
                            stats[i].nullCount++;
                        } else {
                            // Track unique values (limited)
                            if (uniqueValues[i].size() < MAX_UNIQUE_TRACK) {
                                uniqueValues[i].insert(value);
                            }
                        }
                    }

                    rowCount++;
                }

                lineStart = pos + 1;

                // Report progress
                if (!progressCallback.isNull() && rowCount % 10000 == 0) {
                    double progress = static_cast<double>(pos) / csvContent.size() * 100;
                    progressCallback(val(progress));
                }
            }
            pos++;
        }

        totalRows = rowCount;

        // Detect column types from samples
        for (size_t i = 0; i < numColumns; i++) {
            columnTypes[i] = detectColumnType(sampleData[i]);
            stats[i].type = columnTypes[i];
            stats[i].uniqueCount = uniqueValues[i].size();
        }

        // Second pass: calculate detailed statistics
        pos = firstNewline + 1;
        lineStart = pos;

        while (pos < csvContent.size()) {
            if (csvContent[pos] == '\n' || pos == csvContent.size() - 1) {
                size_t lineEnd = (csvContent[pos] == '\n') ? pos : pos + 1;
                size_t lineLength = lineEnd - lineStart;

                if (lineLength > 0) {
                    const auto& fields = parser.parseLine(csvContent.c_str() + lineStart, lineLength);

                    for (size_t i = 0; i < numColumns && i < fields.size(); i++) {
                        const std::string& value = fields[i];

                        if (!TypeChecker::isNull(value)) {
                            DataType type = columnTypes[i];

                            if (type == DataType::INTEGER || type == DataType::FLOAT) {
                                try {
                                    double num = std::stod(value);
                                    stats[i].addNumericValue(num);
                                } catch (...) {}
                            } else if (type == DataType::STRING) {
                                uint32_t len = value.length();
                                stats[i].minLength = std::min(stats[i].minLength, len);
                                stats[i].maxLength = std::max(stats[i].maxLength, len);
                            } else if (type == DataType::BOOLEAN) {
                                std::string lower = value;
                                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                                if (lower == "true" || lower == "yes" || lower == "1") {
                                    stats[i].trueCount++;
                                } else {
                                    stats[i].falseCount++;
                                }
                            } else if (type == DataType::DATE) {
                                if (stats[i].minDate.empty() || value < stats[i].minDate) {
                                    stats[i].minDate = value;
                                }
                                if (stats[i].maxDate.empty() || value > stats[i].maxDate) {
                                    stats[i].maxDate = value;
                                }
                            }
                        }
                    }
                }

                lineStart = pos + 1;
            }
            pos++;
        }

        // Fix minLength for columns with no string data
        for (auto& stat : stats) {
            if (stat.minLength == UINT32_MAX) stat.minLength = 0;
        }

        // Generate JSON output (streaming style for memory efficiency)
        return generateJSON(csvContent, filename);
    }

    std::string generateJSON(const std::string& csvContent, const std::string& filename) {
        std::ostringstream json;
        json << std::fixed << std::setprecision(2);

        // Timestamp
        std::time_t now = std::time(nullptr);
        char timestamp[30];
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

        json << "{\n";

        // Metadata
        json << "  \"metadata\": {\n";
        json << "    \"filename\": \"" << escapeJson(filename) << "\",\n";
        json << "    \"total_rows\": " << totalRows << ",\n";
        json << "    \"total_columns\": " << headers.size() << ",\n";
        json << "    \"file_size_bytes\": " << totalBytes << ",\n";
        json << "    \"encoding\": \"UTF-8\",\n";
        json << "    \"delimiter\": \"" << (delimiter == '\t' ? "\\t" : std::string(1, delimiter)) << "\",\n";
        json << "    \"has_header\": true,\n";
        json << "    \"created_at\": \"" << timestamp << "\",\n";
        json << "    \"processing_info\": {\n";
        json << "      \"processor\": \"WASM Optimized\",\n";
        json << "      \"chunk_size\": " << CHUNK_SIZE << ",\n";
        json << "      \"sample_rows_for_type_detection\": " << SAMPLE_ROWS << "\n";
        json << "    }\n";
        json << "  },\n";

        // Schema
        json << "  \"schema\": {\n";
        json << "    \"columns\": [\n";
        for (size_t i = 0; i < headers.size(); i++) {
            json << "      {\n";
            json << "        \"index\": " << i << ",\n";
            json << "        \"name\": \"" << escapeJson(headers[i]) << "\",\n";
            json << "        \"type\": \"" << dataTypeToString(columnTypes[i]) << "\",\n";
            json << "        \"nullable\": " << (stats[i].nullCount > 0 ? "true" : "false") << ",\n";
            json << "        \"unique\": " << (stats[i].uniqueCount == totalRows - stats[i].nullCount ? "true" : "false") << "\n";
            json << "      }" << (i < headers.size() - 1 ? "," : "") << "\n";
        }
        json << "    ]\n";
        json << "  },\n";

        // Statistics
        json << "  \"statistics\": {\n";
        json << "    \"columns\": {\n";
        for (size_t i = 0; i < headers.size(); i++) {
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

            json << "\n      }" << (i < headers.size() - 1 ? "," : "") << "\n";
        }
        json << "    }\n";
        json << "  },\n";

        // Data (streaming output)
        json << "  \"data\": [\n";

        size_t pos = csvContent.find('\n') + 1;
        size_t lineStart = pos;
        size_t rowIndex = 0;

        while (pos < csvContent.size()) {
            if (csvContent[pos] == '\n' || pos == csvContent.size() - 1) {
                size_t lineEnd = (csvContent[pos] == '\n') ? pos : pos + 1;
                size_t lineLength = lineEnd - lineStart;

                if (lineLength > 0) {
                    const auto& fields = parser.parseLine(csvContent.c_str() + lineStart, lineLength);

                    json << "    {";
                    for (size_t c = 0; c < headers.size(); c++) {
                        std::string value = (c < fields.size()) ? fields[c] : "";
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

                        if (c < headers.size() - 1) json << ",";
                    }
                    json << "\n    }";

                    rowIndex++;
                    if (rowIndex < totalRows) json << ",";
                    json << "\n";
                }

                lineStart = pos + 1;
            }
            pos++;
        }

        json << "  ],\n";

        // Errors
        json << "  \"errors\": []\n";
        json << "}";

        return json.str();
    }

    // Convert with only metadata and stats (no data) for very large files
    std::string processLargeCSVMetadataOnly(const std::string& csvContent, const std::string& filename) {
        totalBytes = csvContent.size();

        size_t firstNewline = csvContent.find('\n');
        if (firstNewline == std::string::npos) {
            return R"({"error": "Invalid CSV: no newline found"})";
        }

        std::string firstLine = csvContent.substr(0, firstNewline);
        delimiter = detectDelimiter(firstLine);
        parser.setDelimiter(delimiter);

        headers = std::vector<std::string>(parser.parseLine(firstLine.c_str(), firstLine.length()));
        size_t numColumns = headers.size();

        stats.resize(numColumns);
        columnTypes.resize(numColumns, DataType::STRING);
        uniqueValues.resize(numColumns);

        std::vector<std::vector<std::string>> sampleData(numColumns);

        size_t pos = firstNewline + 1;
        size_t lineStart = pos;
        size_t rowCount = 0;

        while (pos < csvContent.size()) {
            if (csvContent[pos] == '\n' || pos == csvContent.size() - 1) {
                size_t lineEnd = (csvContent[pos] == '\n') ? pos : pos + 1;
                size_t lineLength = lineEnd - lineStart;

                if (lineLength > 0) {
                    const auto& fields = parser.parseLine(csvContent.c_str() + lineStart, lineLength);

                    if (rowCount < SAMPLE_ROWS) {
                        for (size_t i = 0; i < numColumns && i < fields.size(); i++) {
                            sampleData[i].push_back(fields[i]);
                        }
                    }

                    for (size_t i = 0; i < numColumns && i < fields.size(); i++) {
                        const std::string& value = fields[i];

                        if (TypeChecker::isNull(value)) {
                            stats[i].nullCount++;
                        } else {
                            if (uniqueValues[i].size() < MAX_UNIQUE_TRACK) {
                                uniqueValues[i].insert(value);
                            }

                            // Update numeric stats on the fly
                            if (rowCount >= SAMPLE_ROWS &&
                                (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT)) {
                                try {
                                    double num = std::stod(value);
                                    stats[i].addNumericValue(num);
                                } catch (...) {}
                            }
                        }
                    }

                    rowCount++;

                    // Detect types after collecting samples
                    if (rowCount == SAMPLE_ROWS) {
                        for (size_t i = 0; i < numColumns; i++) {
                            columnTypes[i] = detectColumnType(sampleData[i]);
                            stats[i].type = columnTypes[i];
                        }
                        // Process sample data for statistics
                        for (size_t i = 0; i < numColumns; i++) {
                            if (columnTypes[i] == DataType::INTEGER || columnTypes[i] == DataType::FLOAT) {
                                for (const auto& val : sampleData[i]) {
                                    if (!TypeChecker::isNull(val)) {
                                        try {
                                            double num = std::stod(val);
                                            stats[i].addNumericValue(num);
                                        } catch (...) {}
                                    }
                                }
                            }
                        }
                        sampleData.clear(); // Free memory
                    }
                }

                lineStart = pos + 1;

                if (!progressCallback.isNull() && rowCount % 50000 == 0) {
                    double progress = static_cast<double>(pos) / csvContent.size() * 100;
                    progressCallback(val(progress));
                }
            }
            pos++;
        }

        totalRows = rowCount;

        // If we didn't reach SAMPLE_ROWS, detect types now
        if (rowCount < SAMPLE_ROWS) {
            for (size_t i = 0; i < numColumns; i++) {
                columnTypes[i] = detectColumnType(sampleData[i]);
                stats[i].type = columnTypes[i];
                stats[i].uniqueCount = uniqueValues[i].size();
            }
        }

        for (size_t i = 0; i < numColumns; i++) {
            stats[i].uniqueCount = uniqueValues[i].size();
        }

        // Generate metadata-only JSON
        return generateMetadataOnlyJSON(filename);
    }

    std::string generateMetadataOnlyJSON(const std::string& filename) {
        std::ostringstream json;
        json << std::fixed << std::setprecision(2);

        std::time_t now = std::time(nullptr);
        char timestamp[30];
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));

        json << "{\n";

        // Metadata
        json << "  \"metadata\": {\n";
        json << "    \"filename\": \"" << escapeJson(filename) << "\",\n";
        json << "    \"total_rows\": " << totalRows << ",\n";
        json << "    \"total_columns\": " << headers.size() << ",\n";
        json << "    \"file_size_bytes\": " << totalBytes << ",\n";
        json << "    \"encoding\": \"UTF-8\",\n";
        json << "    \"delimiter\": \"" << (delimiter == '\t' ? "\\t" : std::string(1, delimiter)) << "\",\n";
        json << "    \"has_header\": true,\n";
        json << "    \"created_at\": \"" << timestamp << "\",\n";
        json << "    \"data_included\": false,\n";
        json << "    \"processing_info\": {\n";
        json << "      \"processor\": \"WASM Optimized (Metadata Only)\",\n";
        json << "      \"reason\": \"Large file - data excluded to save memory\"\n";
        json << "    }\n";
        json << "  },\n";

        // Schema
        json << "  \"schema\": {\n";
        json << "    \"columns\": [\n";
        for (size_t i = 0; i < headers.size(); i++) {
            json << "      {\n";
            json << "        \"index\": " << i << ",\n";
            json << "        \"name\": \"" << escapeJson(headers[i]) << "\",\n";
            json << "        \"type\": \"" << dataTypeToString(columnTypes[i]) << "\",\n";
            json << "        \"nullable\": " << (stats[i].nullCount > 0 ? "true" : "false") << "\n";
            json << "      }" << (i < headers.size() - 1 ? "," : "") << "\n";
        }
        json << "    ]\n";
        json << "  },\n";

        // Statistics
        json << "  \"statistics\": {\n";
        json << "    \"columns\": {\n";
        for (size_t i = 0; i < headers.size(); i++) {
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

            json << "\n      }" << (i < headers.size() - 1 ? "," : "") << "\n";
        }
        json << "    }\n";
        json << "  },\n";

        json << "  \"data\": [],\n";
        json << "  \"errors\": []\n";
        json << "}";

        return json.str();
    }
};

// Global processor instance
ChunkedCSVProcessor globalProcessor;

// Exported functions
std::string convertToJson(const std::string& csvContent, const std::string& filename) {
    ChunkedCSVProcessor processor;
    return processor.processLargeCSV(csvContent, filename);
}

std::string convertToJsonMetadataOnly(const std::string& csvContent, const std::string& filename) {
    ChunkedCSVProcessor processor;
    return processor.processLargeCSVMetadataOnly(csvContent, filename);
}

// Auto-select based on file size
std::string convertToJsonAuto(const std::string& csvContent, const std::string& filename) {
    ChunkedCSVProcessor processor;

    // If file is larger than 10MB, use metadata-only mode
    if (csvContent.size() > 10 * 1024 * 1024) {
        return processor.processLargeCSVMetadataOnly(csvContent, filename);
    }

    return processor.processLargeCSV(csvContent, filename);
}

EMSCRIPTEN_BINDINGS(csv_converter_optimized) {
    function("convertToJson", &convertToJson);
    function("convertToJsonMetadataOnly", &convertToJsonMetadataOnly);
    function("convertToJsonAuto", &convertToJsonAuto);
}
