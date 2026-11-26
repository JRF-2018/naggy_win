// Protocol.h - naggy-backend line protocol helpers (portable C++).
#pragma once
#include <string>
#include <vector>

namespace naggy {

std::string ToUtf8(const std::wstring& s);
std::wstring FromUtf8(const std::string& s);

// Naggy::escape_string / unescape_string.
std::wstring EscapeString(const std::wstring& s);
std::wstring UnescapeString(const std::wstring& s);

// Builds a command line ("cmd arg1 arg2\n") in UTF-8.
std::string BuildCommand(const std::vector<std::wstring>& args);

struct Response {
    enum Type { None, Unit, String, List, Base64, Error } type = None;
    std::wstring str;                             // String / Error message
    std::vector<std::vector<std::wstring>> list;  // List rows (unescaped)
    std::vector<std::wstring> messages;           // "# warn" / "# info"
};

// Incremental response parser: feed lines (without "\n") until Done().
class ResponseParser {
public:
    // Returns true when the response is complete.
    bool FeedLine(const std::string& utf8line);
    bool Done() const { return done_; }
    const Response& Result() const { return res_; }
    void Reset() { *this = ResponseParser(); }

private:
    bool inBlock_ = false;
    bool done_ = false;
    std::vector<std::wstring> body_;
    Response res_;
};

}  // namespace naggy
