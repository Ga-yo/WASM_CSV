#include "csv_parser.h"
#include "csv_utils.h"

using namespace std;

// CSV의 구분자 감지
static char detectDelimiter(const string& content) {
    size_t commaCount = 0, tabCount = 0, semicolonCount = 0;
    bool inQuotes = false;
    size_t lineCount = 0;

    // 최적화를 위해 파일의 처음 ~ 5줄까지 분석 
    for (size_t i = 0; i < content.length() && lineCount < 5; i++) {
        char c = content[i];
        if (c == '"') { // 큰따옴표 안에 있는지 확인
            inQuotes = !inQuotes;
        } else if (!inQuotes) { // 큰따옴표 안에 있지 않을 때만 구분자 계산
            if (c == ',') commaCount++;
            else if (c == '\t') tabCount++;
            else if (c == ';') semicolonCount++;
            else if (c == '\n') lineCount++;
        }
    }

    // 최다사용 구분자 반환
    if (tabCount > commaCount && tabCount > semicolonCount) return '\t';
    if (semicolonCount > commaCount && semicolonCount > tabCount) return ';';
    return ','; // default to comma
}

// CSV 헤더 및 행 파싱
CSVParseResult parseCSV(const string& content) {
    CSVParseResult result;
    result.delimiter = detectDelimiter(content); // 구분자 감지

    vector<string> currentRow; // 현재 처리 중인 행을 저장합니다.
    string field;              // 현재 필드(셀)의 내용을 임시 저장합니다.
    bool inQuotes = false;     // 큰따옴표 안에 있는지 여부를 추적합니다.
    bool isFirstRow = true;    // 첫 번째 행이 헤더인지 여부를 확인합니다.

    for (size_t i = 0; i < content.length(); i++) {
        char c = content[i];

        if (c == '"') {
            // 큰따옴표를 여는/닫는지 확인 문자 
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
            // 줄바꿈 문자를 만나면 행 처리를 완료합니다.
            if (c == '\r' && i + 1 < content.length() && content[i + 1] == '\n') i++; // CRLF 처리

            currentRow.push_back(trim(field));
            field.clear();

            bool isEmpty = true;
            for (const auto& f : currentRow) {
                if (!f.empty()) { isEmpty = false; break; }
            }

            if (!isEmpty) {
                if (isFirstRow) {
                    result.headers = currentRow; // 첫 행 = 헤더
                    isFirstRow = false;
                } else {
                    result.rows.push_back(currentRow); // 나머지 행 = 데이터
                }
            }
            currentRow.clear();
        } else {
            // 그 외의 문자는 현재 필드에 추가합니다.
            field += c;
        }
    }

    // 파일의 마지막 부분에 남아있는 데이터를 처리합니다.
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