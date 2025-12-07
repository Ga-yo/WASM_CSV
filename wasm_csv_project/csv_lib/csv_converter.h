#ifndef CSV_CONVERTER_H
#define CSV_CONVERTER_H

#include <string>

// The main optimized conversion function that all others will call.
std::string convertToJsonOptimized(const std::string& csvContent, const std::string& filename);

// Wrapper functions for API compatibility, exposed to JavaScript.
std::string convertToJson(const std::string& csvContent, const std::string& filename);
std::string convertToJsonMetadataOnly(const std::string& csvContent, const std::string& filename);
std::string convertToJsonAuto(const std::string& csvContent, const std::string& filename);

#endif // CSV_CONVERTER_H