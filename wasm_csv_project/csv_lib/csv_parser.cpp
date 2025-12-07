#include "csv_parser.h"
#include "csv_utils.h" // For trim()

using namespace std;

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