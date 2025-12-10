#ifndef CSV_PARSER_H
#define CSV_PARSER_H

#include <string>
#include "csv_types.h" // For CSVParseResult

using namespace std;

char detectDelimiter(const string& content);
CSVParseResult parseCSV(const string& content);

#endif // CSV_PARSER_H