#ifndef CSV_TYPES_H
#define CSV_TYPES_H

#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

// =========================
// Type Definitions
// =========================

enum class DataType {
    INTEGER,
    FLOAT,
    BOOLEAN,
    DATE,
    STRING
};

std::string dataTypeToString(DataType type);

// =========================
// Data Structures
// =========================

struct CSVParseResult {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    char delimiter;
};

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
    double m2 = 0; // For variance calculation
};

void addNumericValue(ColumnStats& stats, double value);
double getStdDev(const ColumnStats& stats);

#endif // CSV_TYPES_H