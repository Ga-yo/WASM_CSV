#ifndef CSV_PARSER_H
#define CSV_PARSER_H

#include <string>
#include "csv_types.h" // For CSVParseResult

// Detects the most likely delimiter (',', '\t', ';') in the CSV content.
char detectDelimiter(const std::string& content);

// Parses the CSV content into headers and rows.
CSVParseResult parseCSV(const std::string& content);

#endif // CSV_PARSER_H