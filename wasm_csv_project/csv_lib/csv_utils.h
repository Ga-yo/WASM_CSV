#ifndef CSV_UTILS_H
#define CSV_UTILS_H

#include <string>
using namespace std;

string trim(const string& str);
string removeBOM(const string& str);
string normalizeLineEndings(const string& str);
string escapeJson(const string& str);
string cleanNumericString(const string& input);

#endif // CSV_UTILS_H