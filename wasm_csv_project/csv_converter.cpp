#include <emscripten/emscripten.h>
#include <emscripten/bind.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <iomanip>
#include <ctime>

using namespace emscripten;

// Utility functions
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

std::string escapeJson(const std::string& str) {
    std::string result;
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

std::string dataTypeToString(DataType type) {
    switch (type) {
        case DataType::INTEGER: return "integer";
        case DataType::FLOAT: return "float";
        case DataType::BOOLEAN: return "boolean";
        case DataType::DATE: return "date";
        case DataType::STRING: return "string";
    }
    return "string";
}

bool isInteger(const std::string& str) {
    if (str.empty()) return false;
    std::string s = trim(str);
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.length()) return false;
    for (size_t i = start; i < s.length(); i++) {
        if (!isdigit(s[i])) return false;
    }
    return true;
}

bool isFloat(const std::string& str) {
    if (str.empty()) return false;
    std::string s = trim(str);
    if (s.empty()) return false;
    bool hasDecimal = false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start >= s.length()) return false;
    for (size_t i = start; i < s.length(); i++) {
        if (s[i] == '.') {
            if (hasDecimal) return false;
            hasDecimal = true;
        } else if (!isdigit(s[i])) {
            return false;
        }
    }
    return hasDecimal;
}

bool isBoolean(const std::string& str) {
    std::string s = trim(str);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s == "true" || s == "false" || s == "yes" || s == "no" || s == "1" || s == "0";
}

bool isDate(const std::string& str) {
    std::string s = trim(str);
    // YYYY-MM-DD or YYYY/MM/DD
    std::regex datePattern(R"(\d{4}[-/]\d{2}[-/]\d{2})");
    return std::regex_match(s, datePattern);
}

bool isNull(const std::string& str) {
    std::string s = trim(str);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s.empty() || s == "null" || s == "na" || s == "n/a" || s == "nan" || s == "-";
}

DataType detectType(const std::vector<std::string>& values) {
    bool allInteger = true;
    bool allFloat = true;
    bool allBoolean = true;
    bool allDate = true;
    int nonNullCount = 0;

    for (const auto& val : values) {
        if (isNull(val)) continue;
        nonNullCount++;

        if (!isInteger(val)) allInteger = false;
        if (!isFloat(val) && !isInteger(val)) allFloat = false;
        if (!isBoolean(val)) allBoolean = false;
        if (!isDate(val)) allDate = false;
    }

    if (nonNullCount == 0) return DataType::STRING;
    if (allBoolean) return DataType::BOOLEAN;
    if (allDate) return DataType::DATE;
    if (allInteger) return DataType::INTEGER;
    if (allFloat) return DataType::FLOAT;
    return DataType::STRING;
}

// CSV Parser
std::vector<std::string> parseCSVLine(const std::string& line, char delimiter = ',') {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.length(); i++) {
        char c = line[i];

        if (c == '"') {
            if (inQuotes && i + 1 < line.length() && line[i + 1] == '"') {
                field += '"';
                i++;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == delimiter && !inQuotes) {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(trim(field));

    return fields;
}

// Statistics calculation
struct ColumnStats {
    DataType type;
    int nullCount = 0;
    int uniqueCount = 0;
    double min = 0;
    double max = 0;
    double sum = 0;
    double mean = 0;
    std::vector<double> numericValues;
    int minLength = INT_MAX;
    int maxLength = 0;
    int trueCount = 0;
    int falseCount = 0;
    std::string minDate;
    std::string maxDate;
};

std::string convertToJson(const std::string& csvContent, const std::string& filename) {
    std::istringstream stream(csvContent);
    std::string line;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> headers;

    // Detect delimiter
    char delimiter = ',';
    if (std::getline(stream, line)) {
        if (line.find('\t') != std::string::npos && line.find(',') == std::string::npos) {
            delimiter = '\t';
        } else if (line.find(';') != std::string::npos && line.find(',') == std::string::npos) {
            delimiter = ';';
        }
        headers = parseCSVLine(line, delimiter);
    }

    // Parse all rows
    while (std::getline(stream, line)) {
        if (trim(line).empty()) continue;
        rows.push_back(parseCSVLine(line, delimiter));
    }

    int numColumns = headers.size();
    int numRows = rows.size();

    // Collect column values and detect types
    std::vector<std::vector<std::string>> columnValues(numColumns);
    for (const auto& row : rows) {
        for (int i = 0; i < numColumns && i < (int)row.size(); i++) {
            columnValues[i].push_back(row[i]);
        }
    }

    // Detect types and calculate statistics
    std::vector<ColumnStats> stats(numColumns);
    for (int i = 0; i < numColumns; i++) {
        stats[i].type = detectType(columnValues[i]);
        std::set<std::string> uniqueValues;

        for (const auto& val : columnValues[i]) {
            if (isNull(val)) {
                stats[i].nullCount++;
                continue;
            }

            uniqueValues.insert(val);

            if (stats[i].type == DataType::INTEGER || stats[i].type == DataType::FLOAT) {
                double num = std::stod(val);
                stats[i].numericValues.push_back(num);
                stats[i].sum += num;
                if (stats[i].numericValues.size() == 1) {
                    stats[i].min = stats[i].max = num;
                } else {
                    stats[i].min = std::min(stats[i].min, num);
                    stats[i].max = std::max(stats[i].max, num);
                }
            } else if (stats[i].type == DataType::STRING) {
                int len = val.length();
                stats[i].minLength = std::min(stats[i].minLength, len);
                stats[i].maxLength = std::max(stats[i].maxLength, len);
            } else if (stats[i].type == DataType::BOOLEAN) {
                std::string lower = val;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower == "true" || lower == "yes" || lower == "1") {
                    stats[i].trueCount++;
                } else {
                    stats[i].falseCount++;
                }
            } else if (stats[i].type == DataType::DATE) {
                if (stats[i].minDate.empty() || val < stats[i].minDate) {
                    stats[i].minDate = val;
                }
                if (stats[i].maxDate.empty() || val > stats[i].maxDate) {
                    stats[i].maxDate = val;
                }
            }
        }

        stats[i].uniqueCount = uniqueValues.size();

        if (!stats[i].numericValues.empty()) {
            stats[i].mean = stats[i].sum / stats[i].numericValues.size();
        }

        if (stats[i].minLength == INT_MAX) stats[i].minLength = 0;
    }

    // Build JSON output
    std::ostringstream json;
    json << std::fixed << std::setprecision(2);

    // Get current timestamp
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
        json << "        \"type\": \"" << dataTypeToString(stats[i].type) << "\",\n";
        json << "        \"nullable\": " << (stats[i].nullCount > 0 ? "true" : "false") << ",\n";
        json << "        \"unique\": " << (stats[i].uniqueCount == numRows - stats[i].nullCount ? "true" : "false") << "\n";
        json << "      }" << (i < numColumns - 1 ? "," : "") << "\n";
    }
    json << "    ]\n";
    json << "  },\n";

    // Statistics
    json << "  \"statistics\": {\n";
    json << "    \"columns\": {\n";
    for (int i = 0; i < numColumns; i++) {
        json << "      \"" << escapeJson(headers[i]) << "\": {\n";
        json << "        \"type\": \"" << dataTypeToString(stats[i].type) << "\",\n";
        json << "        \"null_count\": " << stats[i].nullCount << ",\n";
        json << "        \"unique_count\": " << stats[i].uniqueCount;

        if (stats[i].type == DataType::INTEGER || stats[i].type == DataType::FLOAT) {
            json << ",\n        \"min\": " << stats[i].min;
            json << ",\n        \"max\": " << stats[i].max;
            json << ",\n        \"mean\": " << stats[i].mean;
        } else if (stats[i].type == DataType::STRING) {
            json << ",\n        \"min_length\": " << stats[i].minLength;
            json << ",\n        \"max_length\": " << stats[i].maxLength;
        } else if (stats[i].type == DataType::BOOLEAN) {
            json << ",\n        \"true_count\": " << stats[i].trueCount;
            json << ",\n        \"false_count\": " << stats[i].falseCount;
        } else if (stats[i].type == DataType::DATE) {
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

            if (isNull(value)) {
                json << "null";
            } else if (stats[c].type == DataType::INTEGER) {
                json << value;
            } else if (stats[c].type == DataType::FLOAT) {
                json << std::stod(value);
            } else if (stats[c].type == DataType::BOOLEAN) {
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

    // Errors (empty for now)
    json << "  \"errors\": []\n";

    json << "}";

    return json.str();
}

EMSCRIPTEN_BINDINGS(csv_converter) {
    function("convertToJson", &convertToJson);
}
