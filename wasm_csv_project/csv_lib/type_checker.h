#ifndef TYPE_CHECKER_H
#define TYPE_CHECKER_H

#include <string>

class TypeChecker {
public:
    static bool isInteger(const std::string& str);
    static bool isFloat(const std::string& str);
    static bool isNumeric(const std::string& str);
    static bool isBoolean(const std::string& str);
    static bool isDate(const std::string& str);
    static bool isNull(const std::string& str);
};

#endif // TYPE_CHECKER_H