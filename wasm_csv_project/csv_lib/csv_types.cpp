#include "csv_types.h"
#include <algorithm> // For std::min/max

std::string dataTypeToString(DataType type) {
    switch (type) {
        case DataType::INTEGER: return "integer";
        case DataType::FLOAT:   return "float";
        case DataType::BOOLEAN: return "boolean";
        case DataType::DATE:    return "date";
        case DataType::STRING:  return "string";
    }
    return "string"; // Default case
}

void addNumericValue(ColumnStats& stats, double value) {
    stats.count++;
    stats.sum += value;

    if (stats.count == 1) {
        stats.min = stats.max = value;
        stats.mean = value;
    } else {
        stats.min = std::min(stats.min, value);
        stats.max = std::max(stats.max, value);

        double delta = value - stats.mean;
        stats.mean += delta / stats.count;
        double delta2 = value - stats.mean;
        stats.m2 += delta * delta2;
    }
}

double getStdDev(const ColumnStats& stats) {
    return stats.count > 1 ? sqrt(stats.m2 / (stats.count - 1)) : NAN;
}